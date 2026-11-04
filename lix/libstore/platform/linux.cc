#include "config.h"
#include "lix/libstore/build/personality.hh"
#include "lix/libstore/build/request.capnp.h"
#include "lix/libstore/build/worker.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/cgroup.hh"
#include "lix/libutil/concepts.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/finally.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/platform/linux.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/types.hh"

#include <atomic>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <grp.h>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <net/if.h>
#include <netinet/in.h>
#include <numeric>
#include <optional>
#include <regex>
#include <sched.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <type_traits>

#if __linux__
#include <linux/capability.h>
#endif

#if HAVE_SECCOMP
#include <linux/filter.h>
#include <sys/syscall.h>
#include <seccomp.h>
#endif

namespace fs = std::filesystem;

namespace nix {

namespace {
/**
 * The system for which Nix is compiled.
 */
[[gnu::unused]]
constexpr const std::string_view nativeSystem = SYSTEM;

struct CloneStack
{
    // default stack size for children. 64k should be plenty for our purposees.
    static constexpr size_t SIZE = 65536;

    using Deleter = decltype([](void * v) {
        try {
            if (munmap(v, SIZE)) {
                throw SysError("unmapping stack");
            }
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    });

    std::unique_ptr<void, Deleter> raw;

    CloneStack()
    {
        auto tmp = mmap(0, SIZE, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (tmp == MAP_FAILED) {
            throw SysError("allocating stack");
        }
        raw.reset(tmp);
    }

    void * top()
    {
        return static_cast<char *>(raw.get()) + SIZE;
    }
};
}

/**
 * clone()s the process and runs the callback in the child, using the callback return
 * value as the exit status of the child process. the SIGCHLD flag is always added by
 * this function and need not be provided by the caller due to Pid::wait constraints.
 */
static Pid inClone(CloneStack & stack, int flags, InvocableR<int> auto fn)
{
    auto childFn = [](void * arg) {
        try {
            return (*static_cast<decltype(fn) *>(arg))();
        } catch (...) {
            return 255;
        }
    };
    Pid result{clone(childFn, stack.top(), flags | SIGCHLD, &fn)};
    if (!result) {
        throw SysError("clone() failed");
    }
    return result;
}

/**
 * runs a callback in a vforked child process that shares its address space with
 * the current process. the child behaves much like a thread as a result and the
 * callback must not make changes to process memory that we cannot undo from the
 * parent, otherwise we may leak memory or fully trash the parent address space.
 *
 * NOTE: vfork children that wish to use `setuid`, `setgid`, or `setgroups` must
 * use raw syscalls for this purpose. linux has per-thread credentials while the
 * posix standard mandates per-*process* credentials and libc must this wrap all
 * of these; see `nptl(7)` for the full list. capabilities are *not* affected by
 * such wrapping. we also don't make any provisions for signal safety, a vforked
 * child shares signal handlers with the parent and can thus handle signals that
 * should have been handled by the parent. as such we **must not** use handlers,
 * only signalfd and other cooperative signal mechanisms are fully safe. we have
 * only one handler with code (SIGSEGV), which immediately crashes lix. while we
 * do *register* other handlers they execute no code and are thus not dangerous.
 *
 * \return pid and the result of the callback function (if the child has exited)
 */
static auto asVFork(int flags, auto fn) -> std::pair<Pid, std::optional<Result<decltype(fn())>>>
{
    std::optional<Result<decltype(fn())>> result;

    CloneStack stack;
    auto child = inClone(stack, flags | CLONE_VM | CLONE_VFORK, [&] {
        try {
            result = result::success(fn());
        } catch (...) {
            result = result::current_exception();
        }
        // not necessary because we exit soon, but the compiler may like it
        std::atomic_thread_fence(std::memory_order::release);
        return 0;
    });

    while (true) {
        int status;
        auto result = waitpid(child.get(), &status, WNOHANG);
        if (result == child.get()) {
            child.release(); // it's gone, don't wait for it again
            if (!statusOk(status)) {
                throw Error("failed to run vfork child: %s", statusToString(status));
            }
            break;
        } else if (result == 0) {
            break; // still running, so no exceptions thrown by callback
        } else if (errno != EINTR) {
            throw SysError("cannot get exit status of PID %d", child.get());
        }
    }

    // synchronize with vfork child. if the compiler doesn't treat syscalls
    // as optimization barriers for stack variables we would end up with an
    // incorrect result value, and barriers are cheap compared to syscalls.
    std::atomic_thread_fence(std::memory_order::acquire);
    return {std::move(child), std::move(result)};
}

/**
 * runs a callback in a vforked child process that shares its address space with
 * the current process. the child behaves much like a thread as a result and the
 * callback must not make changes to process memory that we cannot undo from the
 * parent, otherwise we may leak memory or fully trash the parent address space.
 *
 * NOTE: see `asVFork` for safety information regarding credentials and signals.
 *
 * throws an exception if the child exec's or otherwise doesn't return a result.
 */
static auto inVFork(int flags, auto fn)
{
    auto [pid, result] = asVFork(flags, fn);

    if (result) {
        return std::move(result->value());
    } else {
        throw Error("vfork child unexpectedly did not produce a value");
    }
}

static void raiseAmbientCaps(std::span<const unsigned> caps)
{
    // we do the capability dance like this to avoid a dependency
    // on libcap, which has a rather large build closure and many
    // more features that we need for now. maybe some other time.
    static constexpr uint32_t LINUX_CAPABILITY_VERSION_3 = 0x20080522;
    static constexpr uint32_t LINUX_CAPABILITY_U32S_3 = 2;
    struct user_cap_header_struct
    {
        uint32_t version;
        int pid;
    } hdr = {LINUX_CAPABILITY_VERSION_3, 0};
    struct user_cap_data_struct
    {
        uint32_t effective;
        uint32_t permitted;
        uint32_t inheritable;
    } data[LINUX_CAPABILITY_U32S_3] = {};
    for (auto cap : caps) {
        assert(cap / 32 < LINUX_CAPABILITY_U32S_3);
        data[cap / 32].permitted |= 1 << (cap % 32);
        data[cap / 32].inheritable |= 1 << (cap % 32);
    }
    if (syscall(SYS_capset, &hdr, data)) {
        throw SysError("couldn't set capabilities");
    }

    for (auto cap : caps) {
        // We might be running on a kernel older than the API headers, lacking some capabilities.
        // While capset will silently ignore them, PR_CAP_AMBIENT_RAISE fails with EINVAL.
        // Swallow the error ourselves to not introduce unnecessary failures.
        if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0 && errno != EINVAL) {
            throw SysError("couldn't set ambient caps");
        }
    }
}

static Pid launchPasta(
    const AutoCloseFD & logFD,
    const Path & pasta,
    std::initializer_list<const char *> args,
    const AutoCloseFD & netns,
    const AutoCloseFD & userns,
    std::optional<uid_t> uid,
    std::optional<gid_t> gid
)
{
    // this is almost stringsToCharPtrs, but skips unnecessary string allocations
    std::vector<const char *> execArgs;

    execArgs.reserve(args.size() + 6); // 1 for argv0, 4 for namespaces, 1 for trailing nullptr
    execArgs.push_back(baseNameOf(pasta).data());
    std::ranges::copy(args, std::back_inserter(execArgs));
    execArgs.push_back("--netns");
    execArgs.push_back("/proc/self/fd/0");
    if (userns) {
        execArgs.push_back("--userns");
        execArgs.push_back("/proc/self/fd/1");
    }
    execArgs.push_back(nullptr);

    static constexpr unsigned ROOT_CAPS[] = {CAP_SYS_ADMIN, CAP_NET_BIND_SERVICE};
    const bool needsCaps = geteuid() == 0;

    auto [pid, result] = asVFork(/* flags */ 0, [&] -> std::tuple<> {
        // TODO these redirections are crimes. pasta closes all non-stdio file
        // descriptors very early and lacks fd arguments for the namespaces we
        // want it to join. we cannot have pasta join the namespaces via pids;
        // doing so requires capabilities which pasta *also* drops very early.
        if (dup2(netns.get(), 0) == -1) {
            throw SysError("dupping netns fd for pasta");
        }
        closeOnExec(0, false);
        if (userns) {
            if (dup2(userns.get(), 1) == -1) {
                throw SysError("dupping userns fd for pasta");
            }
            closeOnExec(1, false);
        }
        if (needsCaps && prctl(PR_SET_KEEPCAPS, 1) < 0) {
            throw SysError("setting keep-caps failed");
        }
        if (gid && syscall(SYS_setgid, *gid) == -1) {
            throw SysError("setgid failed");
        }
        /* Drop all other groups if we're setgid. */
        if (gid && syscall(SYS_setgroups, 0, 0) == -1 && errno != EPERM) {
            throw SysError("setgroups failed");
        }
        if (uid && syscall(SYS_setuid, *uid) == -1) {
            throw SysError("setuid failed");
        }
        if (needsCaps) {
            if (prctl(PR_SET_KEEPCAPS, 0)) {
                throw SysError("clearing keep-caps failed");
            }

            raiseAmbientCaps(ROOT_CAPS);
        }

        restoreProcessContext();

        // NOLINTNEXTLINE(lix-unsafe-c-calls): pasta is a setting, the args came from C strings
        ::execv(pasta.c_str(), const_cast<char * const *>(execArgs.data()));
        throw SysError("could not exec pasta");
    });

    if (result) {
        (void) result->value(); // must be an exception
    }

    return std::move(pid);
}

void registerLocalStore() {
    StoreImplementations::addConfigOnly<LocalStoreConfig>();
}

static void readProcLink(const std::string & file, UncheckedRoots & roots)
{
    constexpr auto bufsiz = PATH_MAX;
    char buf[bufsiz];
    auto res = sys::readlink(file, buf, bufsiz);
    if (res == -1) {
        if (errno == ENOENT || errno == EACCES || errno == ESRCH) {
            return;
        }
        throw SysError("reading symlink");
    }
    if (res == bufsiz) {
        throw Error("overly long symlink starting with '%1%'", std::string_view(buf, bufsiz));
    }
    if (res > 0 && buf[0] == '/') {
        roots[std::string(static_cast<char *>(buf), res)].emplace(file);
    }
}

static void readFileRoots(const char * path, UncheckedRoots & roots)
{
    try {
        roots[readFile(path)].emplace(path);
    } catch (SysError & e) {
        if (e.errNo != ENOENT && e.errNo != EACCES) {
            throw;
        }
    }
}

LinuxLocalDerivationGoal::~LinuxLocalDerivationGoal()
{
    // pasta being left around mostly happens when builds are aborted
    if (pastaPid) {
        pastaPid.kill();
    }
}

kj::Promise<Result<void>> LinuxLocalStore::findPlatformRoots(UncheckedRoots & unchecked)
try {
    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        auto digitsRegex = regex::parse(R"(^\d+$)");
        auto mapRegex = regex::parse(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = regex::storePathRegex(config().storeDir);
        while (errno = 0, ent = readdir(procDir.get())) {
            checkInterrupt();
            if (std::regex_match(ent->d_name, digitsRegex)) {
                try {
                    readProcLink(fmt("/proc/%s/exe", ent->d_name), unchecked);
                    readProcLink(fmt("/proc/%s/cwd", ent->d_name), unchecked);

                    auto fdStr = fmt("/proc/%s/fd", ent->d_name);
                    auto fdDir = sys::opendir(fdStr);
                    if (!fdDir) {
                        if (errno == ENOENT || errno == EACCES) {
                            continue;
                        }
                        throw SysError("opening %1%", fdStr);
                    }
                    struct dirent * fd_ent;
                    while (errno = 0, fd_ent = readdir(fdDir.get())) {
                        if (fd_ent->d_name[0] != '.') {
                            readProcLink(fmt("%s/%s", fdStr, fd_ent->d_name), unchecked);
                        }
                    }
                    if (errno) {
                        if (errno == ESRCH) {
                            continue;
                        }
                        throw SysError("iterating /proc/%1%/fd", ent->d_name);
                    }
                    fdDir.reset();

                    auto mapFile = fmt("/proc/%s/maps", ent->d_name);
                    auto mapLines =
                        tokenizeString<std::vector<std::string>>(readFile(mapFile), "\n");
                    for (const auto & line : mapLines) {
                        auto match = std::smatch{};
                        if (std::regex_match(line, match, mapRegex)) {
                            unchecked[match[1]].emplace(mapFile);
                        }
                    }

                    auto envFile = fmt("/proc/%s/environ", ent->d_name);
                    auto envString = readFile(envFile);
                    auto env_end = std::sregex_iterator{};
                    for (auto i =
                             std::sregex_iterator{
                                 envString.begin(), envString.end(), storePathRegex
                             };
                         i != env_end;
                         ++i)
                    {
                        unchecked[i->str()].emplace(envFile);
                    }
                } catch (SysError & e) {
                    if (errno == ENOENT || errno == EACCES || errno == ESRCH) {
                        continue;
                    }
                    throw;
                }
            }
        }
        if (errno) {
            throw SysError("iterating /proc");
        }
    }

    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

#if HAVE_SECCOMP

/**
 * Look up the syscall number of a syscall name.
 *
 * The syscall lists below are written in terms of syscall names instead of
 * SCMP_SYS() constants because the kernel headers we compile against and the
 * syscall table of the libseccomp we link against are versioned independently.
 */
static int syscallNumber(const std::string & name)
{
    int nr = seccomp_syscall_resolve_name(requireCString(name));
    if (nr == __NR_SCMP_ERROR) {
        throw Error(
            "libseccomp does not know the syscall '%1%', so Lix cannot set up its "
            "syscall filter. This build of Lix needs a newer libseccomp.",
            name
        );
    }
    return nr;
}

static void addRule(
    scmp_filter_ctx ctx,
    uint32_t action,
    const char * name,
    const struct scmp_arg_cmp * args,
    unsigned int argsCount
)
{
    int rc = seccomp_rule_add_array(ctx, action, syscallNumber(name), argsCount, args);
    if (rc != 0) {
        throw Error(
            "unable to add a rule for the syscall '%s' to the seccomp filter "
            "(libseccomp returned %i)",
            name,
            rc
        );
    }
}

/*
 * We do not use SCMP_SYS() because macro arguments are expanded before use.
 * libc headers define some syscall names as macros, e.g. musl: prlimit64 is prlimit,
 * stat64 is stat, etc.
 *
 * It would therefore check the wrong syscall or fail to compile.
 *
 * We do the paste in each of the macros below and not in a helper macro due to that.
 */
#define ALLOW_SYSCALL(ctx, name)                         \
    do {                                                 \
        static_assert(                                   \
            __SNR_##name != __NR_SCMP_ERROR,             \
            #name                                        \
            " is not known to "                          \
            "this libseccomp"                            \
        );                                               \
        addRule(ctx, SCMP_ACT_ALLOW, #name, nullptr, 0); \
    } while (0)

#define DENY_SYSCALL(ctx, name, errno_)                          \
    do {                                                         \
        static_assert(                                           \
            __SNR_##name != __NR_SCMP_ERROR,                     \
            #name                                                \
            " is not known to "                                  \
            "this libseccomp"                                    \
        );                                                       \
        addRule(ctx, SCMP_ACT_ERRNO(errno_), #name, nullptr, 0); \
    } while (0)

#define ALLOW_CHMOD_IF_SAFE(ctx, name, modePos)                                                         \
    do {                                                                                                \
        const struct scmp_arg_cmp safeMode = SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID | S_ISGID, 0); \
        const struct scmp_arg_cmp setuidMode = SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID);   \
        const struct scmp_arg_cmp setgidMode = SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID);   \
        static_assert(                                                                                  \
            __SNR_##name != __NR_SCMP_ERROR,                                                            \
            #name                                                                                       \
            " is not known to "                                                                         \
            "this libseccomp"                                                                           \
        );                                                                                              \
        addRule(ctx, SCMP_ACT_ALLOW, #name, &safeMode, 1);                                              \
        addRule(ctx, SCMP_ACT_ERRNO(EPERM), #name, &setuidMode, 1);                                     \
        addRule(ctx, SCMP_ACT_ERRNO(EPERM), #name, &setgidMode, 1);                                     \
    } while (0)

static std::vector<struct sock_filter> compileSyscallFilter()
{
    scmp_filter_ctx ctx;

    // Pretend that syscalls we don't yet know about don't exist.
    // This is the best option for compatibility: after all, they did in fact not exist not too long ago.
    if (!(ctx = seccomp_init(SCMP_ACT_ERRNO(ENOSYS))))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS) != 0)
        printError("unable to add mips seccomp architecture");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS64N32) != 0)
        printError("unable to add mips64-*abin32 seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL) != 0)
        printError("unable to add mipsel seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL64N32) != 0)
        printError("unable to add mips64el-*abin32 seccomp architecture");

    // This list is intended for machine consumption.
    // Please keep its format, order and BEGIN/END markers.
    //
    // The list must stay in sync with the syscall table of the libseccomp we
    // link against. Run check-syscalls to determine which new syscalls should
    // be added.
    // New syscalls must be audited and handled in a way that blocks the following dangerous operations:
    // * Creation of non-empty setuid/setgid files
    // * Creation of extended attributes (including ACLs)
    //
    // BEGIN extract-syscalls
    ALLOW_SYSCALL(ctx, accept);
    ALLOW_SYSCALL(ctx, accept4);
    ALLOW_SYSCALL(ctx, access);
    ALLOW_SYSCALL(ctx, acct);
    ALLOW_SYSCALL(ctx, add_key);
    ALLOW_SYSCALL(ctx, adjtimex);
    ALLOW_SYSCALL(ctx, afs_syscall);
    ALLOW_SYSCALL(ctx, alarm);
    ALLOW_SYSCALL(ctx, arch_prctl);
    ALLOW_SYSCALL(ctx, arm_fadvise64_64);
    ALLOW_SYSCALL(ctx, arm_sync_file_range);
    // skip atomic_barrier (unsupported architecture)
    // skip atomic_cmpxchg_32 (unsupported architecture)
    ALLOW_SYSCALL(ctx, bdflush);
    ALLOW_SYSCALL(ctx, bind);
    ALLOW_SYSCALL(ctx, bpf);
    ALLOW_SYSCALL(ctx, break);
    ALLOW_SYSCALL(ctx, breakpoint);
    ALLOW_SYSCALL(ctx, brk);
    ALLOW_SYSCALL(ctx, cachectl);
    ALLOW_SYSCALL(ctx, cacheflush);
    ALLOW_SYSCALL(ctx, cachestat);
    ALLOW_SYSCALL(ctx, capget);
    ALLOW_SYSCALL(ctx, capset);
    ALLOW_SYSCALL(ctx, chdir);
    // skip chmod (dangerous)
    ALLOW_SYSCALL(ctx, chown);
    ALLOW_SYSCALL(ctx, chown32);
    ALLOW_SYSCALL(ctx, chroot);
    ALLOW_SYSCALL(ctx, clock_adjtime);
    ALLOW_SYSCALL(ctx, clock_adjtime64);
    ALLOW_SYSCALL(ctx, clock_getres);
    ALLOW_SYSCALL(ctx, clock_getres_time64);
    ALLOW_SYSCALL(ctx, clock_gettime);
    ALLOW_SYSCALL(ctx, clock_gettime64);
    ALLOW_SYSCALL(ctx, clock_nanosleep);
    ALLOW_SYSCALL(ctx, clock_nanosleep_time64);
    ALLOW_SYSCALL(ctx, clock_settime);
    ALLOW_SYSCALL(ctx, clock_settime64);
    ALLOW_SYSCALL(ctx, clone);
    ALLOW_SYSCALL(ctx, clone3);
    ALLOW_SYSCALL(ctx, close);
    ALLOW_SYSCALL(ctx, close_range);
    ALLOW_SYSCALL(ctx, connect);
    ALLOW_SYSCALL(ctx, copy_file_range);
    ALLOW_SYSCALL(ctx, creat);
    ALLOW_SYSCALL(ctx, create_module);
    ALLOW_SYSCALL(ctx, delete_module);
    ALLOW_SYSCALL(ctx, dup);
    ALLOW_SYSCALL(ctx, dup2);
    ALLOW_SYSCALL(ctx, dup3);
    ALLOW_SYSCALL(ctx, epoll_create);
    ALLOW_SYSCALL(ctx, epoll_create1);
    ALLOW_SYSCALL(ctx, epoll_ctl);
    ALLOW_SYSCALL(ctx, epoll_ctl_old);
    ALLOW_SYSCALL(ctx, epoll_pwait);
    ALLOW_SYSCALL(ctx, epoll_pwait2);
    ALLOW_SYSCALL(ctx, epoll_wait);
    ALLOW_SYSCALL(ctx, epoll_wait_old);
    ALLOW_SYSCALL(ctx, eventfd);
    ALLOW_SYSCALL(ctx, eventfd2);
    ALLOW_SYSCALL(ctx, execve);
    ALLOW_SYSCALL(ctx, execveat);
    ALLOW_SYSCALL(ctx, exit);
    ALLOW_SYSCALL(ctx, exit_group);
    ALLOW_SYSCALL(ctx, faccessat);
    ALLOW_SYSCALL(ctx, faccessat2);
    ALLOW_SYSCALL(ctx, fadvise64);
    ALLOW_SYSCALL(ctx, fadvise64_64);
    ALLOW_SYSCALL(ctx, fallocate);
    ALLOW_SYSCALL(ctx, fanotify_init);
    ALLOW_SYSCALL(ctx, fanotify_mark);
    ALLOW_SYSCALL(ctx, fchdir);
    // skip fchmod (dangerous)
    // skip fchmodat (dangerous)
    // skip fchmodat2 (dangerous)
    ALLOW_SYSCALL(ctx, fchown);
    ALLOW_SYSCALL(ctx, fchown32);
    ALLOW_SYSCALL(ctx, fchownat);
    ALLOW_SYSCALL(ctx, fcntl);
    ALLOW_SYSCALL(ctx, fcntl64);
    ALLOW_SYSCALL(ctx, fdatasync);
    ALLOW_SYSCALL(ctx, fgetxattr);
    ALLOW_SYSCALL(ctx, file_getattr);
    ALLOW_SYSCALL(ctx, file_setattr);
    ALLOW_SYSCALL(ctx, finit_module);
    ALLOW_SYSCALL(ctx, flistxattr);
    ALLOW_SYSCALL(ctx, flock);
    ALLOW_SYSCALL(ctx, fork);
    ALLOW_SYSCALL(ctx, fremovexattr);
    ALLOW_SYSCALL(ctx, fsconfig);
    // skip fsetxattr (dangerous)
    ALLOW_SYSCALL(ctx, fsmount);
    ALLOW_SYSCALL(ctx, fsopen);
    ALLOW_SYSCALL(ctx, fspick);
    ALLOW_SYSCALL(ctx, fstat);
    ALLOW_SYSCALL(ctx, fstat64);
    ALLOW_SYSCALL(ctx, fstatat64);
    ALLOW_SYSCALL(ctx, fstatfs);
    ALLOW_SYSCALL(ctx, fstatfs64);
    ALLOW_SYSCALL(ctx, fsync);
    ALLOW_SYSCALL(ctx, ftime);
    ALLOW_SYSCALL(ctx, ftruncate);
    ALLOW_SYSCALL(ctx, ftruncate64);
    ALLOW_SYSCALL(ctx, futex);
    ALLOW_SYSCALL(ctx, futex_requeue);
    ALLOW_SYSCALL(ctx, futex_time64);
    ALLOW_SYSCALL(ctx, futex_wait);
    ALLOW_SYSCALL(ctx, futex_waitv);
    ALLOW_SYSCALL(ctx, futex_wake);
    ALLOW_SYSCALL(ctx, futimesat);
    ALLOW_SYSCALL(ctx, getcpu);
    ALLOW_SYSCALL(ctx, getcwd);
    ALLOW_SYSCALL(ctx, getdents);
    ALLOW_SYSCALL(ctx, getdents64);
    ALLOW_SYSCALL(ctx, getegid);
    ALLOW_SYSCALL(ctx, getegid32);
    ALLOW_SYSCALL(ctx, geteuid);
    ALLOW_SYSCALL(ctx, geteuid32);
    ALLOW_SYSCALL(ctx, getgid);
    ALLOW_SYSCALL(ctx, getgid32);
    ALLOW_SYSCALL(ctx, getgroups);
    ALLOW_SYSCALL(ctx, getgroups32);
    ALLOW_SYSCALL(ctx, getitimer);
    ALLOW_SYSCALL(ctx, get_kernel_syms);
    ALLOW_SYSCALL(ctx, get_mempolicy);
    // skip getpagesize (unsupported architecture)
    ALLOW_SYSCALL(ctx, getpeername);
    ALLOW_SYSCALL(ctx, getpgid);
    ALLOW_SYSCALL(ctx, getpgrp);
    ALLOW_SYSCALL(ctx, getpid);
    ALLOW_SYSCALL(ctx, getpmsg);
    ALLOW_SYSCALL(ctx, getppid);
    ALLOW_SYSCALL(ctx, getpriority);
    ALLOW_SYSCALL(ctx, getrandom);
    ALLOW_SYSCALL(ctx, getresgid);
    ALLOW_SYSCALL(ctx, getresgid32);
    ALLOW_SYSCALL(ctx, getresuid);
    ALLOW_SYSCALL(ctx, getresuid32);
    ALLOW_SYSCALL(ctx, getrlimit);
    ALLOW_SYSCALL(ctx, get_robust_list);
    ALLOW_SYSCALL(ctx, getrusage);
    ALLOW_SYSCALL(ctx, getsid);
    ALLOW_SYSCALL(ctx, getsockname);
    ALLOW_SYSCALL(ctx, getsockopt);
    ALLOW_SYSCALL(ctx, get_thread_area);
    ALLOW_SYSCALL(ctx, gettid);
    ALLOW_SYSCALL(ctx, gettimeofday);
    ALLOW_SYSCALL(ctx, get_tls);
    ALLOW_SYSCALL(ctx, getuid);
    ALLOW_SYSCALL(ctx, getuid32);
    ALLOW_SYSCALL(ctx, getxattr);
    ALLOW_SYSCALL(ctx, getxattrat);
    ALLOW_SYSCALL(ctx, gtty);
    ALLOW_SYSCALL(ctx, idle);
    ALLOW_SYSCALL(ctx, init_module);
    ALLOW_SYSCALL(ctx, inotify_add_watch);
    ALLOW_SYSCALL(ctx, inotify_init);
    ALLOW_SYSCALL(ctx, inotify_init1);
    ALLOW_SYSCALL(ctx, inotify_rm_watch);
    ALLOW_SYSCALL(ctx, io_cancel);
    ALLOW_SYSCALL(ctx, ioctl);
    ALLOW_SYSCALL(ctx, io_destroy);
    ALLOW_SYSCALL(ctx, io_getevents);
    ALLOW_SYSCALL(ctx, ioperm);
    ALLOW_SYSCALL(ctx, io_pgetevents);
    ALLOW_SYSCALL(ctx, io_pgetevents_time64);
    ALLOW_SYSCALL(ctx, iopl);
    ALLOW_SYSCALL(ctx, ioprio_get);
    ALLOW_SYSCALL(ctx, ioprio_set);
    ALLOW_SYSCALL(ctx, io_setup);
    ALLOW_SYSCALL(ctx, io_submit);
    // skip io_uring_enter (may become dangerous)
    // skip io_uring_register (may become dangerous)
    // skip io_uring_setup (may become dangerous)
    ALLOW_SYSCALL(ctx, ipc);
    ALLOW_SYSCALL(ctx, kcmp);
    ALLOW_SYSCALL(ctx, kexec_file_load);
    ALLOW_SYSCALL(ctx, kexec_load);
    ALLOW_SYSCALL(ctx, keyctl);
    ALLOW_SYSCALL(ctx, kill);
    ALLOW_SYSCALL(ctx, landlock_add_rule);
    ALLOW_SYSCALL(ctx, landlock_create_ruleset);
    ALLOW_SYSCALL(ctx, landlock_restrict_self);
    ALLOW_SYSCALL(ctx, lchown);
    ALLOW_SYSCALL(ctx, lchown32);
    ALLOW_SYSCALL(ctx, lgetxattr);
    ALLOW_SYSCALL(ctx, link);
    ALLOW_SYSCALL(ctx, linkat);
    ALLOW_SYSCALL(ctx, listen);
    ALLOW_SYSCALL(ctx, listmount);
    ALLOW_SYSCALL(ctx, listns);
    ALLOW_SYSCALL(ctx, listxattr);
    ALLOW_SYSCALL(ctx, listxattrat);
    ALLOW_SYSCALL(ctx, llistxattr);
    ALLOW_SYSCALL(ctx, _llseek);
    ALLOW_SYSCALL(ctx, lock);
    ALLOW_SYSCALL(ctx, lookup_dcookie);
    ALLOW_SYSCALL(ctx, lremovexattr);
    ALLOW_SYSCALL(ctx, lseek);
    // skip lsetxattr (dangerous)
    ALLOW_SYSCALL(ctx, lsm_get_self_attr);
    ALLOW_SYSCALL(ctx, lsm_list_modules);
    // skip lsm_set_self_attr (dangerous)
    ALLOW_SYSCALL(ctx, lstat);
    ALLOW_SYSCALL(ctx, lstat64);
    ALLOW_SYSCALL(ctx, madvise);
    ALLOW_SYSCALL(ctx, map_shadow_stack);
    ALLOW_SYSCALL(ctx, mbind);
    ALLOW_SYSCALL(ctx, membarrier);
    ALLOW_SYSCALL(ctx, memfd_create);
    ALLOW_SYSCALL(ctx, memfd_secret);
    ALLOW_SYSCALL(ctx, migrate_pages);
    ALLOW_SYSCALL(ctx, mincore);
    ALLOW_SYSCALL(ctx, mkdir);
    ALLOW_SYSCALL(ctx, mkdirat);
    ALLOW_SYSCALL(ctx, mknod);
    ALLOW_SYSCALL(ctx, mknodat);
    ALLOW_SYSCALL(ctx, mlock);
    ALLOW_SYSCALL(ctx, mlock2);
    ALLOW_SYSCALL(ctx, mlockall);
    ALLOW_SYSCALL(ctx, mmap);
    ALLOW_SYSCALL(ctx, mmap2);
    ALLOW_SYSCALL(ctx, modify_ldt);
    ALLOW_SYSCALL(ctx, mount);
    ALLOW_SYSCALL(ctx, mount_setattr);
    ALLOW_SYSCALL(ctx, move_mount);
    ALLOW_SYSCALL(ctx, move_pages);
    ALLOW_SYSCALL(ctx, mprotect);
    ALLOW_SYSCALL(ctx, mpx);
    ALLOW_SYSCALL(ctx, mq_getsetattr);
    ALLOW_SYSCALL(ctx, mq_notify);
    ALLOW_SYSCALL(ctx, mq_open);
    ALLOW_SYSCALL(ctx, mq_timedreceive);
    ALLOW_SYSCALL(ctx, mq_timedreceive_time64);
    ALLOW_SYSCALL(ctx, mq_timedsend);
    ALLOW_SYSCALL(ctx, mq_timedsend_time64);
    ALLOW_SYSCALL(ctx, mq_unlink);
    ALLOW_SYSCALL(ctx, mremap);
    ALLOW_SYSCALL(ctx, mseal);
    ALLOW_SYSCALL(ctx, msgctl);
    ALLOW_SYSCALL(ctx, msgget);
    ALLOW_SYSCALL(ctx, msgrcv);
    ALLOW_SYSCALL(ctx, msgsnd);
    ALLOW_SYSCALL(ctx, msync);
    ALLOW_SYSCALL(ctx, multiplexer);
    ALLOW_SYSCALL(ctx, munlock);
    ALLOW_SYSCALL(ctx, munlockall);
    ALLOW_SYSCALL(ctx, munmap);
    ALLOW_SYSCALL(ctx, name_to_handle_at);
    ALLOW_SYSCALL(ctx, nanosleep);
    ALLOW_SYSCALL(ctx, newfstatat);
    ALLOW_SYSCALL(ctx, _newselect);
    ALLOW_SYSCALL(ctx, nfsservctl);
    ALLOW_SYSCALL(ctx, nice);
    ALLOW_SYSCALL(ctx, oldfstat);
    ALLOW_SYSCALL(ctx, oldlstat);
    ALLOW_SYSCALL(ctx, oldolduname);
    ALLOW_SYSCALL(ctx, oldstat);
    ALLOW_SYSCALL(ctx, olduname);
    ALLOW_SYSCALL(ctx, open);
    ALLOW_SYSCALL(ctx, openat);
    ALLOW_SYSCALL(ctx, openat2);
    ALLOW_SYSCALL(ctx, open_by_handle_at);
    ALLOW_SYSCALL(ctx, open_tree);
    ALLOW_SYSCALL(ctx, open_tree_attr);
    ALLOW_SYSCALL(ctx, pause);
    ALLOW_SYSCALL(ctx, pciconfig_iobase);
    ALLOW_SYSCALL(ctx, pciconfig_read);
    ALLOW_SYSCALL(ctx, pciconfig_write);
    ALLOW_SYSCALL(ctx, perf_event_open);
    ALLOW_SYSCALL(ctx, personality);
    ALLOW_SYSCALL(ctx, pidfd_getfd);
    ALLOW_SYSCALL(ctx, pidfd_open);
    ALLOW_SYSCALL(ctx, pidfd_send_signal);
    ALLOW_SYSCALL(ctx, pipe);
    ALLOW_SYSCALL(ctx, pipe2);
    ALLOW_SYSCALL(ctx, pivot_root);
    ALLOW_SYSCALL(ctx, pkey_alloc);
    ALLOW_SYSCALL(ctx, pkey_free);
    ALLOW_SYSCALL(ctx, pkey_mprotect);
    ALLOW_SYSCALL(ctx, poll);
    ALLOW_SYSCALL(ctx, ppoll);
    ALLOW_SYSCALL(ctx, ppoll_time64);
    ALLOW_SYSCALL(ctx, prctl);
    ALLOW_SYSCALL(ctx, pread64);
    ALLOW_SYSCALL(ctx, preadv);
    ALLOW_SYSCALL(ctx, preadv2);
    ALLOW_SYSCALL(ctx, prlimit64);
    ALLOW_SYSCALL(ctx, process_madvise);
    ALLOW_SYSCALL(ctx, process_mrelease);
    ALLOW_SYSCALL(ctx, process_vm_readv);
    ALLOW_SYSCALL(ctx, process_vm_writev);
    ALLOW_SYSCALL(ctx, prof);
    ALLOW_SYSCALL(ctx, profil);
    ALLOW_SYSCALL(ctx, pselect6);
    ALLOW_SYSCALL(ctx, pselect6_time64);
    ALLOW_SYSCALL(ctx, ptrace);
    ALLOW_SYSCALL(ctx, putpmsg);
    ALLOW_SYSCALL(ctx, pwrite64);
    ALLOW_SYSCALL(ctx, pwritev);
    ALLOW_SYSCALL(ctx, pwritev2);
    ALLOW_SYSCALL(ctx, query_module);
    ALLOW_SYSCALL(ctx, quotactl);
    ALLOW_SYSCALL(ctx, quotactl_fd);
    ALLOW_SYSCALL(ctx, read);
    ALLOW_SYSCALL(ctx, readahead);
    ALLOW_SYSCALL(ctx, readdir);
    ALLOW_SYSCALL(ctx, readlink);
    ALLOW_SYSCALL(ctx, readlinkat);
    ALLOW_SYSCALL(ctx, readv);
    ALLOW_SYSCALL(ctx, reboot);
    ALLOW_SYSCALL(ctx, recv);
    ALLOW_SYSCALL(ctx, recvfrom);
    ALLOW_SYSCALL(ctx, recvmmsg);
    ALLOW_SYSCALL(ctx, recvmmsg_time64);
    ALLOW_SYSCALL(ctx, recvmsg);
    ALLOW_SYSCALL(ctx, remap_file_pages);
    ALLOW_SYSCALL(ctx, removexattr);
    ALLOW_SYSCALL(ctx, removexattrat);
    ALLOW_SYSCALL(ctx, rename);
    ALLOW_SYSCALL(ctx, renameat);
    ALLOW_SYSCALL(ctx, renameat2);
    ALLOW_SYSCALL(ctx, request_key);
    ALLOW_SYSCALL(ctx, restart_syscall);
    ALLOW_SYSCALL(ctx, riscv_flush_icache);
    ALLOW_SYSCALL(ctx, riscv_hwprobe);
    ALLOW_SYSCALL(ctx, rmdir);
    ALLOW_SYSCALL(ctx, rseq);
    ALLOW_SYSCALL(ctx, rseq_slice_yield);
    ALLOW_SYSCALL(ctx, rtas);
    ALLOW_SYSCALL(ctx, rt_sigaction);
    ALLOW_SYSCALL(ctx, rt_sigpending);
    ALLOW_SYSCALL(ctx, rt_sigprocmask);
    ALLOW_SYSCALL(ctx, rt_sigqueueinfo);
    ALLOW_SYSCALL(ctx, rt_sigreturn);
    ALLOW_SYSCALL(ctx, rt_sigsuspend);
    ALLOW_SYSCALL(ctx, rt_sigtimedwait);
    ALLOW_SYSCALL(ctx, rt_sigtimedwait_time64);
    ALLOW_SYSCALL(ctx, rt_tgsigqueueinfo);
    ALLOW_SYSCALL(ctx, s390_guarded_storage);
    ALLOW_SYSCALL(ctx, s390_pci_mmio_read);
    ALLOW_SYSCALL(ctx, s390_pci_mmio_write);
    ALLOW_SYSCALL(ctx, s390_runtime_instr);
    ALLOW_SYSCALL(ctx, s390_sthyi);
    ALLOW_SYSCALL(ctx, sched_getaffinity);
    ALLOW_SYSCALL(ctx, sched_getattr);
    ALLOW_SYSCALL(ctx, sched_getparam);
    ALLOW_SYSCALL(ctx, sched_get_priority_max);
    ALLOW_SYSCALL(ctx, sched_get_priority_min);
    ALLOW_SYSCALL(ctx, sched_getscheduler);
    ALLOW_SYSCALL(ctx, sched_rr_get_interval);
    ALLOW_SYSCALL(ctx, sched_rr_get_interval_time64);
    ALLOW_SYSCALL(ctx, sched_setaffinity);
    ALLOW_SYSCALL(ctx, sched_setattr);
    ALLOW_SYSCALL(ctx, sched_setparam);
    ALLOW_SYSCALL(ctx, sched_setscheduler);
    ALLOW_SYSCALL(ctx, sched_yield);
    ALLOW_SYSCALL(ctx, seccomp);
    ALLOW_SYSCALL(ctx, security);
    ALLOW_SYSCALL(ctx, select);
    ALLOW_SYSCALL(ctx, semctl);
    ALLOW_SYSCALL(ctx, semget);
    ALLOW_SYSCALL(ctx, semop);
    ALLOW_SYSCALL(ctx, semtimedop);
    ALLOW_SYSCALL(ctx, semtimedop_time64);
    ALLOW_SYSCALL(ctx, send);
    ALLOW_SYSCALL(ctx, sendfile);
    ALLOW_SYSCALL(ctx, sendfile64);
    ALLOW_SYSCALL(ctx, sendmmsg);
    ALLOW_SYSCALL(ctx, sendmsg);
    ALLOW_SYSCALL(ctx, sendto);
    ALLOW_SYSCALL(ctx, setdomainname);
    ALLOW_SYSCALL(ctx, setfsgid);
    ALLOW_SYSCALL(ctx, setfsgid32);
    ALLOW_SYSCALL(ctx, setfsuid);
    ALLOW_SYSCALL(ctx, setfsuid32);
    ALLOW_SYSCALL(ctx, setgid);
    ALLOW_SYSCALL(ctx, setgid32);
    ALLOW_SYSCALL(ctx, setgroups);
    ALLOW_SYSCALL(ctx, setgroups32);
    ALLOW_SYSCALL(ctx, sethostname);
    ALLOW_SYSCALL(ctx, setitimer);
    ALLOW_SYSCALL(ctx, set_mempolicy);
    ALLOW_SYSCALL(ctx, set_mempolicy_home_node);
    ALLOW_SYSCALL(ctx, setns);
    ALLOW_SYSCALL(ctx, setpgid);
    ALLOW_SYSCALL(ctx, setpriority);
    ALLOW_SYSCALL(ctx, setregid);
    ALLOW_SYSCALL(ctx, setregid32);
    ALLOW_SYSCALL(ctx, setresgid);
    ALLOW_SYSCALL(ctx, setresgid32);
    ALLOW_SYSCALL(ctx, setresuid);
    ALLOW_SYSCALL(ctx, setresuid32);
    ALLOW_SYSCALL(ctx, setreuid);
    ALLOW_SYSCALL(ctx, setreuid32);
    ALLOW_SYSCALL(ctx, setrlimit);
    ALLOW_SYSCALL(ctx, set_robust_list);
    ALLOW_SYSCALL(ctx, setsid);
    ALLOW_SYSCALL(ctx, setsockopt);
    ALLOW_SYSCALL(ctx, set_thread_area);
    ALLOW_SYSCALL(ctx, set_tid_address);
    ALLOW_SYSCALL(ctx, settimeofday);
    ALLOW_SYSCALL(ctx, set_tls);
    ALLOW_SYSCALL(ctx, setuid);
    ALLOW_SYSCALL(ctx, setuid32);
    // skip setxattr (dangerous)
    // skip setxattrat (dangerous)
    ALLOW_SYSCALL(ctx, sgetmask);
    ALLOW_SYSCALL(ctx, shmat);
    ALLOW_SYSCALL(ctx, shmctl);
    ALLOW_SYSCALL(ctx, shmdt);
    ALLOW_SYSCALL(ctx, shmget);
    ALLOW_SYSCALL(ctx, shutdown);
    ALLOW_SYSCALL(ctx, sigaction);
    ALLOW_SYSCALL(ctx, sigaltstack);
    ALLOW_SYSCALL(ctx, signal);
    ALLOW_SYSCALL(ctx, signalfd);
    ALLOW_SYSCALL(ctx, signalfd4);
    ALLOW_SYSCALL(ctx, sigpending);
    ALLOW_SYSCALL(ctx, sigprocmask);
    ALLOW_SYSCALL(ctx, sigreturn);
    ALLOW_SYSCALL(ctx, sigsuspend);
    ALLOW_SYSCALL(ctx, socket);
    ALLOW_SYSCALL(ctx, socketcall);
    ALLOW_SYSCALL(ctx, socketpair);
    ALLOW_SYSCALL(ctx, splice);
    ALLOW_SYSCALL(ctx, spu_create);
    ALLOW_SYSCALL(ctx, spu_run);
    ALLOW_SYSCALL(ctx, ssetmask);
    ALLOW_SYSCALL(ctx, stat);
    ALLOW_SYSCALL(ctx, stat64);
    ALLOW_SYSCALL(ctx, statfs);
    ALLOW_SYSCALL(ctx, statfs64);
    ALLOW_SYSCALL(ctx, statmount);
    ALLOW_SYSCALL(ctx, statx);
    ALLOW_SYSCALL(ctx, stime);
    ALLOW_SYSCALL(ctx, stty);
    ALLOW_SYSCALL(ctx, subpage_prot);
    ALLOW_SYSCALL(ctx, swapcontext);
    ALLOW_SYSCALL(ctx, swapoff);
    ALLOW_SYSCALL(ctx, swapon);
    ALLOW_SYSCALL(ctx, switch_endian);
    ALLOW_SYSCALL(ctx, symlink);
    ALLOW_SYSCALL(ctx, symlinkat);
    ALLOW_SYSCALL(ctx, sync);
    ALLOW_SYSCALL(ctx, sync_file_range);
    ALLOW_SYSCALL(ctx, sync_file_range2);
    ALLOW_SYSCALL(ctx, syncfs);
    ALLOW_SYSCALL(ctx, syscall);
    ALLOW_SYSCALL(ctx, _sysctl);
    ALLOW_SYSCALL(ctx, sys_debug_setcontext);
    ALLOW_SYSCALL(ctx, sysfs);
    ALLOW_SYSCALL(ctx, sysinfo);
    ALLOW_SYSCALL(ctx, syslog);
    ALLOW_SYSCALL(ctx, sysmips);
    ALLOW_SYSCALL(ctx, tee);
    ALLOW_SYSCALL(ctx, tgkill);
    ALLOW_SYSCALL(ctx, time);
    ALLOW_SYSCALL(ctx, timer_create);
    ALLOW_SYSCALL(ctx, timer_delete);
    ALLOW_SYSCALL(ctx, timerfd);
    ALLOW_SYSCALL(ctx, timerfd_create);
    ALLOW_SYSCALL(ctx, timerfd_gettime);
    ALLOW_SYSCALL(ctx, timerfd_gettime64);
    ALLOW_SYSCALL(ctx, timerfd_settime);
    ALLOW_SYSCALL(ctx, timerfd_settime64);
    ALLOW_SYSCALL(ctx, timer_getoverrun);
    ALLOW_SYSCALL(ctx, timer_gettime);
    ALLOW_SYSCALL(ctx, timer_gettime64);
    ALLOW_SYSCALL(ctx, timer_settime);
    ALLOW_SYSCALL(ctx, timer_settime64);
    ALLOW_SYSCALL(ctx, times);
    ALLOW_SYSCALL(ctx, tkill);
    ALLOW_SYSCALL(ctx, truncate);
    ALLOW_SYSCALL(ctx, truncate64);
    ALLOW_SYSCALL(ctx, tuxcall);
    ALLOW_SYSCALL(ctx, ugetrlimit);
    ALLOW_SYSCALL(ctx, ulimit);
    ALLOW_SYSCALL(ctx, umask);
    ALLOW_SYSCALL(ctx, umount);
    ALLOW_SYSCALL(ctx, umount2);
    ALLOW_SYSCALL(ctx, uname);
    ALLOW_SYSCALL(ctx, unlink);
    ALLOW_SYSCALL(ctx, unlinkat);
    ALLOW_SYSCALL(ctx, unshare);
    ALLOW_SYSCALL(ctx, uprobe);
    ALLOW_SYSCALL(ctx, uretprobe);
    ALLOW_SYSCALL(ctx, uselib);
    ALLOW_SYSCALL(ctx, userfaultfd);
    ALLOW_SYSCALL(ctx, usr26);
    ALLOW_SYSCALL(ctx, usr32);
    ALLOW_SYSCALL(ctx, ustat);
    ALLOW_SYSCALL(ctx, utime);
    ALLOW_SYSCALL(ctx, utimensat);
    ALLOW_SYSCALL(ctx, utimensat_time64);
    ALLOW_SYSCALL(ctx, utimes);
    ALLOW_SYSCALL(ctx, vfork);
    ALLOW_SYSCALL(ctx, vhangup);
    ALLOW_SYSCALL(ctx, vm86);
    ALLOW_SYSCALL(ctx, vm86old);
    ALLOW_SYSCALL(ctx, vmsplice);
    ALLOW_SYSCALL(ctx, vserver);
    ALLOW_SYSCALL(ctx, wait4);
    ALLOW_SYSCALL(ctx, waitid);
    ALLOW_SYSCALL(ctx, waitpid);
    ALLOW_SYSCALL(ctx, write);
    ALLOW_SYSCALL(ctx, writev);
    // END extract-syscalls

    // chmod family: prevent adding setuid/setgid bits to existing files.
    // The Nix store does not support setuid/setgid, and even their temporary creation can weaken the security of the sandbox.
    ALLOW_CHMOD_IF_SAFE(ctx, chmod, 1);
    ALLOW_CHMOD_IF_SAFE(ctx, fchmod, 1);
    ALLOW_CHMOD_IF_SAFE(ctx, fchmodat, 2);
    ALLOW_CHMOD_IF_SAFE(ctx, fchmodat2, 2);

    // setxattr family: prevent creation of extended attributes or ACLs.
    // Not all filesystems support them, and they're incompatible with the NAR format.
    DENY_SYSCALL(ctx, setxattr, ENOTSUP);
    DENY_SYSCALL(ctx, lsetxattr, ENOTSUP);
    DENY_SYSCALL(ctx, fsetxattr, ENOTSUP);

    Pipe filterPipe;
    filterPipe.create();
    auto filterBytes_ = std::async([&]() {
        return drainFD(filterPipe.readSide.get());
    });
    if (seccomp_export_bpf(ctx, filterPipe.writeSide.get()) != 0)
        throw SysError("unable to compile seccomp BPF program");
    filterPipe.writeSide.close();
    auto filterBytes = filterBytes_.get();

    assert(filterBytes.size() % sizeof(struct sock_filter) == 0);
    std::vector<struct sock_filter> filter(filterBytes.size() / sizeof(struct sock_filter));
    std::memcpy(filter.data(), filterBytes.data(), filterBytes.size());
    return filter;
}

static const std::vector<struct sock_filter> &getSyscallFilter()
{
    static auto filter = compileSyscallFilter();
    return filter;
}

#endif

void LinuxLocalDerivationGoal::prepareSandbox()
{
#if HAVE_SECCOMP
    // Our seccomp filter program is surprisingly expensive to compile (~10ms).
    // For this reason, we precompile it once and then cache it.
    // This has to be done in the parent so that all builds get to use the same cache.
    getSyscallFilter();
#endif

    /* Create a temporary directory in which we set up the chroot
       environment using bind-mounts.  We put it in the Nix store
       to ensure that we can create hard-links to non-directory
       inputs in the fake Nix store in the chroot (see below). */
    chrootRootDir = worker.store.Store::toRealPath(drvPath) + ".chroot";
    deletePath(chrootRootDir);

    /* Clean up the chroot directory automatically. */
    autoDelChroot = std::make_shared<AutoDelete>(chrootRootDir);

    printMsg(lvlChatty, "setting up chroot environment in '%1%'", chrootRootDir);

    // FIXME: make this 0700
    if (sys::mkdir(chrootRootDir, buildUser && buildUser->getUIDCount() != 1 ? 0755 : 0750) == -1) {
        throw SysError("cannot create '%1%'", chrootRootDir);
    }

    if (buildUser
        && sys::chown(
               chrootRootDir,
               buildUser->getUIDCount() != 1 ? buildUser->getUID() : 0,
               buildUser->getGID()
           ) == -1)
    {
        throw SysError("cannot change ownership of '%1%'", chrootRootDir);
    }

    /* Create a writable /tmp in the chroot.  Many builders need
       this.  (Of course they should really respect $TMPDIR
       instead.) */
    Path chrootTmpDir = chrootRootDir + "/tmp";
    createDirs(chrootTmpDir);
    chmodPath(chrootTmpDir, 01777);

    /* Create a /etc/passwd with entries for the build user and the
       nobody account.  The latter is kind of a hack to support
       Samba-in-QEMU. */
    createDirs(chrootRootDir + "/etc");

    if (parsedDrv->useUidRange() && (!buildUser || buildUser->getUIDCount() < 65536))
        throw Error("feature 'uid-range' requires the setting '%s' to be enabled", settings.autoAllocateUids.name);

    if (parsedDrv->useUidRange()) {
        chownToBuilder(chrootRootDir + "/etc");
    }

    writeFile(
        chrootRootDir + "/etc/passwd",
        fmt("root:x:0:0:Nix build user:%3%:/noshell\n"
            "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
            "nobody:x:65534:65534:Nobody:/:/noshell\n",
            sandboxUid(),
            sandboxGid(),
            settings.sandboxBuildDir)
    );

    /* Declare the build user's group so that programs get a consistent
       view of the system (e.g., "id -gn"). */
    writeFile(
        chrootRootDir + "/etc/group",
        fmt("root:x:0:\n"
            "nixbld:!:%1%:\n"
            "nogroup:x:65534:\n",
            sandboxGid())
    );

    /* Fixed-output derivations typically need to access the
       network, so give them access to /etc/resolv.conf and so
       on. */
    if (!derivationType->isSandboxed()) {
        // Only use nss functions to resolve hosts and
        // services. Don’t use it for anything else that may
        // be configured for this system. This limits the
        // potential impurities introduced in fixed-outputs.
        writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

        /* N.B. it is realistic that these paths might not exist. It
           happens when testing Nix building fixed-output derivations
           within a pure derivation. */
        for (auto & path : {"/etc/services", "/etc/hosts"}) {
            if (pathAccessible(path, true)) {
                // Copy the actual file, not the symlink, because we don't know where
                // the symlink is pointing, and we don't want to chase down the entire
                // chain.
                //
                // This means if your network config changes during a FOD build,
                // the DNS in the sandbox will be wrong. However, this is pretty unlikely
                // to actually be a problem, because FODs are generally pretty fast,
                // and machines with often-changing network configurations probably
                // want to run resolved or some other local resolver anyway.
                //
                // There's also just no simple way to do this correctly, you have to manually
                // inotify watch the files for changes on the outside and update the sandbox
                // while the build is running (or at least that's what Flatpak does).
                //
                // I also just generally feel icky about modifying sandbox state under a build,
                // even though it really shouldn't be a big deal. -K900
                copyFile(path, chrootRootDir + path, {.followSymlinks = true});
            } else if (pathExists(path)) {
                // The path exist but we were not able to access it. This is not a fatal
                // error, warn about this so the user can remediate.
                printTaggedWarning(
                    "'%1%' exists but is inaccessible, it will not be copied in the "
                    "sandbox",
                    path
                );
            }
        }

        if (pathAccessible("/etc/resolv.conf", true)) {
            const auto resolvConf = rewriteResolvConf(readFile("/etc/resolv.conf"));
            writeFile(chrootRootDir + "/etc/resolv.conf", resolvConf);
        } else if (pathExists("/etc/resolv.conf")) {
            // The path exist but we were not able to access it. This is not a fatal error,
            // warn about this so the user can remediate.
            printTaggedWarning(
                "'/etc/resolv.conf' exists but is inaccessible, it will not be rewritten "
                "inside the sandbox; DNS operations inside the sandbox may be "
                "non-functional."
            );
        }
    }

    /* Create /etc/hosts with localhost entry. */
    if (derivationType->isSandboxed())
        writeFile(chrootRootDir + "/etc/hosts", "127.0.0.1 localhost\n::1 localhost\n");

    /* Make the closure of the inputs available in the chroot,
       rather than the whole Nix store.  This prevents any access
       to undeclared dependencies.  Directories are bind-mounted,
       while other inputs are hard-linked (since only directories
       can be bind-mounted).  !!! As an extra security
       precaution, make the fake Nix store only writable by the
       build user. */
    Path chrootStoreDir = chrootRootDir + worker.store.config().storeDir;
    createDirs(chrootStoreDir);
    chmodPath(chrootStoreDir, 01775);

    if (buildUser && sys::chown(chrootStoreDir, 0, buildUser->getGID()) == -1) {
        throw SysError("cannot change ownership of '%1%'", chrootStoreDir);
    }

    for (auto & i : inputPaths) {
        auto p = worker.store.printStorePath(i);
        Path r = worker.store.toRealPath(p);
        pathsInChroot.insert_or_assign(p, r);
    }

    /* If we're repairing, checking or rebuilding part of a
       multiple-outputs derivation, it's possible that we're
       rebuilding a path that is in settings.sandbox-paths
       (typically the dependencies of /bin/sh).  Throw them
       out. */
    for (auto & i : drv->outputsAndPaths(worker.store)) {
        pathsInChroot.erase(worker.store.printStorePath(i.second.second));
    }

    if (buildUser && (buildUser->getUIDCount() != 1 || settings.useCgroups)) {
        context.cgroup.emplace(
            settings.nixStateDir + "/cgroups",
            fmt("nix-build@%s-%d", drvPath.hashPart(), buildUser->getUID()),
            buildUser->getUID(),
            buildUser->getGID()
        );

        debug("using cgroup '%s' for build", context.cgroup->name());

        /* TODO(raito): it would be very nice if we could propagate system features
         * based on which cgroup controllers are available in `context.cgroup`
         * so that we would re-schedule any derivation that actually has
         * anti-affinity or pro-affinity with certain cgroup controllers, e.g.
         * a derivation that is very sensitive to the memory cgroup controller
         * for performance reason.
         *
         * Unfortunately, the current design of system features prevent mutation
         * and worse, we are too late for rescheduling this derivation.
         *
         * Therefore, we decide to always copy all the available controllers
         * to the delegated cgroup.
         */
        debug(
            "available cgroup controllers for cgroup '%s': '%s'",
            context.cgroup->name(),
            concatStringsSep(",", context.cgroup->controllers())
        );
    }

    if (parsedDrv->useUidRange() && !context.cgroup) {
        throw Error(
            "feature 'uid-range' requires the setting '%s' to be enabled", settings.useCgroups.name
        );
    }
}

std::string LinuxLocalDerivationGoal::rewriteResolvConf(std::string fromHost)
{
    if (!runPasta()) {
        return fromHost;
    }

    static constexpr auto flags = std::regex::ECMAScript | std::regex::multiline;
    static auto lineRegex = regex::parse("^nameserver\\s.*$", flags);
    std::string nsInSandbox = "\n";
    nsInSandbox += fmt("nameserver %s\n", PASTA_HOST_IPV4);
    nsInSandbox += fmt("nameserver %s\n", PASTA_HOST_IPV6);
    return std::regex_replace(fromHost, lineRegex, "") + nsInSandbox;
}

void LinuxLocalDerivationGoal::fillBuilderConfig(build::Request::Builder request)
{
    auto config = request.getPlatform().initLinux();

#if HAVE_SECCOMP
    {
        const auto & filters = std::span{getSyscallFilter()};
        auto builder = config.initSeccompFilters(filters.size_bytes());
        memcpy(builder.begin(), filters.data(), builder.size());
    }
#endif

    if (useChroot) {
        auto sandbox = config.initSandbox();
        {
            auto builder = sandbox.initPaths(pathsInChroot.size());
            for (const auto & [idx, path] : enumerate(pathsInChroot)) {
                const auto & [target, binding] = path;
                RPC_FILL(builder[idx], setTarget, target);
                RPC_FILL(builder[idx], setSource, binding.source);
                builder[idx].setOptional(binding.optional);
            }
        }
        sandbox.setPrivateNetwork(privateNetwork());
        RPC_FILL(sandbox, setChrootRootDir, chrootRootDir);
        RPC_FILL(sandbox, setStoreDir, worker.store.config().storeDir);
        sandbox.setWantsKvm(settings.systemFeatures.get().contains("kvm"));
        RPC_FILL(sandbox, setSandboxShmFlags, fmt("size=%s", settings.sandboxShmSize));
        sandbox.setUseUidRange(parsedDrv->useUidRange());
        sandbox.setUid(sandboxUid());
        sandbox.setGid(sandboxGid());
        if (runPasta()) {
            sandbox.setWaitForInterface(LinuxLocalDerivationGoal::PASTA_NS_IFNAME);
        }
    }

    RPC_FILL(config, setPlatform, drv->platform);
    config.setParentPid(getpid());
}

Pid LinuxLocalDerivationGoal::startChild(AutoCloseFD setupFD, AutoCloseFD logPTY)
{
    // If we're not sandboxing no need to faff about, use the fallback
    if (!useChroot) {
        return LocalDerivationGoal::startChild(std::move(setupFD), std::move(logPTY));
    }
    /* Set up private namespaces for the build:

       - The PID namespace causes the build to start as PID 1.
         Processes outside of the chroot are not visible to those
         on the inside, but processes inside the chroot are
         visible from the outside (though with different PIDs).

       - The private mount namespace ensures that all the bind
         mounts we do will only show up in this process and its
         children, and will disappear automatically when we're
         done.

       - The private network namespace ensures that the builder
         cannot talk to the outside world (or vice versa).  It
         only has a private loopback interface. If a copy of
         `pasta` is available, Fixed-output derivations are run
         inside a private network namespace with internet
         access, otherwise they are run in the host's network
         namespace, to allow functions like fetchurl to work.

       - The IPC namespace prevents the builder from communicating
         with outside processes using SysV IPC mechanisms (shared
         memory, message queues, semaphores).  It also ensures
         that all IPC objects are destroyed when the builder
         exits.

       - The UTS namespace ensures that builders see a hostname of
         localhost rather than the actual hostname.

       We use a helper process to do the clone() to work around
       clone() being broken in multi-threaded programs due to
       at-fork handlers not being run. Note that we use
       CLONE_PARENT to ensure that the real builder is parented to
       us.
    */

    auto [userns, netns] = [&] -> std::pair<AutoCloseFD, AutoCloseFD> {
        // we always want to create a new network namespace for pasta, even when
        // we can't actually run it. not doing so hides bugs and impairs purity.
        const bool wantNetNS = settings.pastaPath != "" || privateNetwork();
        const bool wantUserNS = worker.namespaces.user;
        if (!wantUserNS && !wantNetNS) {
            return {};
        }

        CloneStack stack;
        // vm and fd table can be *very* large and expensive to clone on busy daemons.
        // since the child only stops itself forever there's no danger in sharing them
        Pid pid{inClone(
            stack,
            (wantUserNS ? CLONE_NEWUSER : 0) | (wantNetNS ? CLONE_NEWNET : 0) | CLONE_VM | CLONE_FILES,
            []() -> int {
                for (;;) {
                    // NOTE: musl apparently caches the pid of the process, which fucks with raise().
                    // we must explicitly use getpid() to bypass this cache instead of using raise; a
                    // raise(SIGSTOP) would stop the *daemon* process, and this breaks sandbox setup.
                    kill(getpid(), SIGSTOP);
                }
            }
        )};

        auto userns = !wantUserNS ? AutoCloseFD{} : [&] {
            auto userns = AutoCloseFD{sys::open(std::format("/proc/{}/ns/user", pid.get()), O_RDONLY)};
            if (!userns) {
                throw SysError("failed to open user namespace");
            }

            /* Set the UID/GID mapping of the builder's user namespace
               such that the sandbox user maps to the build user, or to
               the calling user (if build users are disabled). */
            uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
            uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
            uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

            writeFile(
                std::format("/proc/{}/uid_map", pid.get()), fmt("%d %d %d", sandboxUid(), hostUid, nrIds)
            );

            if (!buildUser || buildUser->getUIDCount() == 1) {
                writeFile(std::format("/proc/{}/setgroups", pid.get()), "deny");
            }

            writeFile(
                std::format("/proc/{}/gid_map", pid.get()), fmt("%d %d %d", sandboxGid(), hostGid, nrIds)
            );
            return userns;
        }();
        if (!userns) {
            debug("note: not using a user namespace");
        }

        auto netns = !wantNetNS ? AutoCloseFD{} : [&] {
            auto netns = AutoCloseFD{sys::open(std::format("/proc/{}/ns/net", pid.get()), O_RDONLY)};
            if (!netns) {
                throw SysError("failed to open net namespace");
            }
            return netns;
        }();

        return {std::move(userns), std::move(netns)};
    }();

    if (runPasta()) {
        // Bring up pasta, for handling FOD networking. We don't let it daemonize
        // itself for process managements reasons and kill it manually when done.

        // FIXME ideally we want a notification when pasta exits, but we cannot do
        // this at present. without such support we need to busy-wait for pasta to
        // set up the namespace completely and time out after a while for the case
        // of pasta launch failures. pasta logs go to syslog only for now as well.
        pastaPid = launchPasta(
            logPTY,
            settings.pastaPath,
            {
                // TODO add a new sandbox mode flag to disable all or parts of this?
                // clang-format off
                "--quiet",
                "--foreground",
                "--config-net",
                "--gateway", PASTA_HOST_IPV4,
                "--address", PASTA_CHILD_IPV4, "--netmask", PASTA_IPV4_NETMASK,
                "--dns-forward", PASTA_HOST_IPV4,
                "--gateway", PASTA_HOST_IPV6,
                "--address", PASTA_CHILD_IPV6,
                "--dns-forward", PASTA_HOST_IPV6,
                "--ns-ifname", PASTA_NS_IFNAME,
                "--no-netns-quit",
                // clang-format on
            },
            netns,
            userns,
            useBuildUsers() ? std::optional(buildUser->getUID()) : std::nullopt,
            useBuildUsers() ? std::optional(buildUser->getGID()) : std::nullopt
        );
    }

    return inVFork(/* flags*/ 0, [&]() {
        if (dup2(setupFD.get(), STDIN_FILENO) == -1) {
            throw SysError("failed to hook up setup fd");
        }
        closeOnExec(STDIN_FILENO, false);
        if (dup2(logPTY.get(), STDERR_FILENO) == -1) {
            throw SysError("failed to redirect build output to log file");
        }

        /* Migrate the child inside the available control group. */
        if (context.cgroup) {
            context.cgroup->adoptProcess(getpid());
        }

        // Drop additional groups here because we can't do it after we're in the
        // new user namespace. check `asVFork` for why we use raw syscalls here.
        if (syscall(SYS_setgroups, 0, nullptr) == -1) {
            if (errno != EPERM) {
                throw SysError("setgroups failed");
            }
            if (settings.requireDropSupplementaryGroups) {
                throw Error(
                    "setgroups failed. Set the require-drop-supplementary-groups option to false to skip "
                    "this step."
                );
            }
        }

        if (userns && setns(userns.get(), 0)) {
            throw SysError("setns(userNS)");
        }
        if (netns && setns(netns.get(), 0)) {
            throw SysError("setns(netNS)");
        }

        CloneStack stack;
        return inClone(
            stack,
            CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | CLONE_VM | CLONE_FILES
                | CLONE_VFORK,
            [&]() -> int {
                if (userns) {
                    std::array<unsigned, CAP_LAST_CAP + 1> caps;
                    std::iota(caps.begin(), caps.end(), 0);
                    raiseAmbientCaps(caps);
                }

                execl(LIX_LIBEXEC_DIR "/launch-builder", "launch-builder", nullptr);
                throw SysError("exec failed");
            }
        );
    });
}

void LinuxLocalDerivationGoal::cleanupHookFinally()
{
    /* This hook is used to release the build users
     * and release the lock on this UID.
     *
     * So we need to ensure that our cgroup business
     * is already done before releasing it,
     * otherwise, another build may grab the UID
     * and start a cgroup with it, resulting
     * in a confusing set of errors.
     *
     * Statistics are stored inside the cgroup
     * object so that `killSandbox` can retrieve
     * them later.
     */
    if (context.cgroup) {
        context.cgroup->destroy();
    }

    LocalDerivationGoal::cleanupHookFinally();
}

void LinuxLocalDerivationGoal::killSandbox(bool getStats)
{
    if (context.cgroup) {
        /* This might have already been killed
         * by the clean-up hook above. */
        context.cgroup->kill();
        if (getStats) {
            auto stats = context.cgroup->getStatistics();
            buildResult.cpuUser = stats.cpuUser;
            buildResult.cpuSystem = stats.cpuSystem;
        }
        /* It may be desireable to destroy the cgroup here
         * but we may be calling this at the start of the build
         * to ensure that no leftover process are running under sandbox UIDs.
         * With control groups, that's already impossible. */
    } else if (!useChroot) {
        /* Linux sandboxes use PID namespaces, which ensure that processes cannot escape from a build.
           Therefore, we don't need to kill all processes belonging to the build user.
           This avoids processes unrelated to the build being killed, thus avoiding: https://git.lix.systems/lix-project/lix/issues/667 */
        LocalDerivationGoal::killSandbox(getStats);
    }

    if (pastaPid) {
        // FIXME we really want to send SIGTERM instead and wait for pasta to exit,
        // but we do not have the infra for that right now. we send SIGKILL instead
        // and treat exiting with that as a successful exit code until such a time.
        // this is not likely to cause problems since pasta runs as the build user,
        // but not inside the build sandbox. if it's killed it's either due to some
        // external influence (in which case the sandboxed child will probably fail
        // due to network errors, if it used the network at all) or some bug in lix
        if (auto status = pastaPid.kill(); !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
            if (WIFSIGNALED(status)) {
                throw Error("pasta killed by signal %i", WTERMSIG(status));
            } else if (WIFEXITED(status)) {
                throw Error("pasta exited with code %i", WEXITSTATUS(status));
            } else {
                throw Error("pasta exited with status %i", status);
            }
        }
    }
}

struct ChrootDirAwareFSAccessor : public LocalStoreAccessor
{
    Path chrootDir;

    ChrootDirAwareFSAccessor(ref<LocalFSStore> store, const Path & chrootDir)
        : LocalStoreAccessor(store)
        , chrootDir(chrootDir)
    {
    }

    kj::Promise<Result<Path>> toRealPath(const Path & path, bool requireValidPath = true) override
    try {
        auto storePath = store->toStorePath(path).first;
        if (!TRY_AWAIT(store->isValidPath(storePath))) {
            auto chrootStorePath = chrootDir + "/" + path;
            if (pathExists(chrootStorePath)) {
                co_return chrootStorePath;
            }

            if (requireValidPath) {
                throw InvalidPath(
                    "path '%1%' does not exist in the store, neither does chrooted path '%2%'",
                    store->printStorePath(storePath),
                    chrootStorePath
                );
            }
        }

        co_return TRY_AWAIT(LocalStoreAccessor::toRealPath(path, false));
    } catch (...) {
        co_return result::current_exception();
    }
};

std::optional<ref<FSAccessor>> LinuxLocalDerivationGoal::getChrootDirAwareFSAccessor()
{
    return make_ref<ChrootDirAwareFSAccessor>(
        ref<LocalFSStore>::unsafeFromPtr(
            std::dynamic_pointer_cast<LocalFSStore>(getLocalStore().shared_from_this())
        ),
        chrootRootDir
    );
}
}
