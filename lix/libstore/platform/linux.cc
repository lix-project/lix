#include "build/personality.hh"
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
#include "lix/libutil/mount.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/platform/linux.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <grp.h>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <net/if.h>
#include <netinet/in.h>
#include <optional>
#include <regex>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#if __linux__
#include <linux/capability.h>
#endif

#if HAVE_SECCOMP
#include <linux/filter.h>
#include <sys/syscall.h>
#include <seccomp.h>
#endif

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
    const std::span<const unsigned> caps =
        geteuid() == 0 ? std::span{ROOT_CAPS} : std::span<const unsigned>{};

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
        if (!caps.empty() && prctl(PR_SET_KEEPCAPS, 1) < 0) {
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
        if (!caps.empty()) {
            if (prctl(PR_SET_KEEPCAPS, 0)) {
                throw SysError("clearing keep-caps failed");
            }

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
                if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0) {
                    throw SysError("couldn't set ambient caps");
                }
            }
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
    StoreImplementations::add<LinuxLocalStore, LocalStoreConfig>();
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

static void allowSyscall(scmp_filter_ctx ctx, int syscall) {
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0) != 0)
        throw SysError("unable to add seccomp rule");
}

#define ALLOW_CHMOD_IF_SAFE(ctx, syscall, modePos) \
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID | S_ISGID, 0)) != 0 || \
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) != 0 || \
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) != 0) \
        throw SysError("unable to add seccomp rule");

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
    // Currently, it is up to date with libseccomp 2.5.5 and glibc 2.39.
    // Run check-syscalls to determine which new syscalls should be added.
    // New syscalls must be audited and handled in a way that blocks the following dangerous operations:
    // * Creation of non-empty setuid/setgid files
    // * Creation of extended attributes (including ACLs)
    //
    // BEGIN extract-syscalls
    allowSyscall(ctx, SCMP_SYS(accept));
    allowSyscall(ctx, SCMP_SYS(accept4));
    allowSyscall(ctx, SCMP_SYS(access));
    allowSyscall(ctx, SCMP_SYS(acct));
    allowSyscall(ctx, SCMP_SYS(add_key));
    allowSyscall(ctx, SCMP_SYS(adjtimex));
    allowSyscall(ctx, SCMP_SYS(afs_syscall));
    allowSyscall(ctx, SCMP_SYS(alarm));
    allowSyscall(ctx, SCMP_SYS(arch_prctl));
    allowSyscall(ctx, SCMP_SYS(arm_fadvise64_64));
    allowSyscall(ctx, SCMP_SYS(arm_sync_file_range));
    allowSyscall(ctx, SCMP_SYS(bdflush));
    allowSyscall(ctx, SCMP_SYS(bind));
    allowSyscall(ctx, SCMP_SYS(bpf));
    allowSyscall(ctx, SCMP_SYS(break));
    allowSyscall(ctx, SCMP_SYS(breakpoint));
    allowSyscall(ctx, SCMP_SYS(brk));
    allowSyscall(ctx, SCMP_SYS(cachectl));
    allowSyscall(ctx, SCMP_SYS(cacheflush));
    allowSyscall(ctx, SCMP_SYS(cachestat));
    allowSyscall(ctx, SCMP_SYS(capget));
    allowSyscall(ctx, SCMP_SYS(capset));
    allowSyscall(ctx, SCMP_SYS(chdir));
    // skip chmod (dangerous)
    allowSyscall(ctx, SCMP_SYS(chown));
    allowSyscall(ctx, SCMP_SYS(chown32));
    allowSyscall(ctx, SCMP_SYS(chroot));
    allowSyscall(ctx, SCMP_SYS(clock_adjtime));
    allowSyscall(ctx, SCMP_SYS(clock_adjtime64));
    allowSyscall(ctx, SCMP_SYS(clock_getres));
    allowSyscall(ctx, SCMP_SYS(clock_getres_time64));
    allowSyscall(ctx, SCMP_SYS(clock_gettime));
    allowSyscall(ctx, SCMP_SYS(clock_gettime64));
    allowSyscall(ctx, SCMP_SYS(clock_nanosleep));
    allowSyscall(ctx, SCMP_SYS(clock_nanosleep_time64));
    allowSyscall(ctx, SCMP_SYS(clock_settime));
    allowSyscall(ctx, SCMP_SYS(clock_settime64));
    allowSyscall(ctx, SCMP_SYS(clone));
    allowSyscall(ctx, SCMP_SYS(clone3));
    allowSyscall(ctx, SCMP_SYS(close));
    allowSyscall(ctx, SCMP_SYS(close_range));
    allowSyscall(ctx, SCMP_SYS(connect));
    allowSyscall(ctx, SCMP_SYS(copy_file_range));
    allowSyscall(ctx, SCMP_SYS(creat));
    allowSyscall(ctx, SCMP_SYS(create_module));
    allowSyscall(ctx, SCMP_SYS(delete_module));
    allowSyscall(ctx, SCMP_SYS(dup));
    allowSyscall(ctx, SCMP_SYS(dup2));
    allowSyscall(ctx, SCMP_SYS(dup3));
    allowSyscall(ctx, SCMP_SYS(epoll_create));
    allowSyscall(ctx, SCMP_SYS(epoll_create1));
    allowSyscall(ctx, SCMP_SYS(epoll_ctl));
    allowSyscall(ctx, SCMP_SYS(epoll_ctl_old));
    allowSyscall(ctx, SCMP_SYS(epoll_pwait));
    allowSyscall(ctx, SCMP_SYS(epoll_pwait2));
    allowSyscall(ctx, SCMP_SYS(epoll_wait));
    allowSyscall(ctx, SCMP_SYS(epoll_wait_old));
    allowSyscall(ctx, SCMP_SYS(eventfd));
    allowSyscall(ctx, SCMP_SYS(eventfd2));
    allowSyscall(ctx, SCMP_SYS(execve));
    allowSyscall(ctx, SCMP_SYS(execveat));
    allowSyscall(ctx, SCMP_SYS(exit));
    allowSyscall(ctx, SCMP_SYS(exit_group));
    allowSyscall(ctx, SCMP_SYS(faccessat));
    allowSyscall(ctx, SCMP_SYS(faccessat2));
    allowSyscall(ctx, SCMP_SYS(fadvise64));
    allowSyscall(ctx, SCMP_SYS(fadvise64_64));
    allowSyscall(ctx, SCMP_SYS(fallocate));
    allowSyscall(ctx, SCMP_SYS(fanotify_init));
    allowSyscall(ctx, SCMP_SYS(fanotify_mark));
    allowSyscall(ctx, SCMP_SYS(fchdir));
    // skip fchmod (dangerous)
    // skip fchmodat (dangerous)
    // skip fchmodat2 (dangerous)
    allowSyscall(ctx, SCMP_SYS(fchown));
    allowSyscall(ctx, SCMP_SYS(fchown32));
    allowSyscall(ctx, SCMP_SYS(fchownat));
    allowSyscall(ctx, SCMP_SYS(fcntl));
    allowSyscall(ctx, SCMP_SYS(fcntl64));
    allowSyscall(ctx, SCMP_SYS(fdatasync));
    allowSyscall(ctx, SCMP_SYS(fgetxattr));
    allowSyscall(ctx, SCMP_SYS(finit_module));
    allowSyscall(ctx, SCMP_SYS(flistxattr));
    allowSyscall(ctx, SCMP_SYS(flock));
    allowSyscall(ctx, SCMP_SYS(fork));
    allowSyscall(ctx, SCMP_SYS(fremovexattr));
    allowSyscall(ctx, SCMP_SYS(fsconfig));
    // skip fsetxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(fsmount));
    allowSyscall(ctx, SCMP_SYS(fsopen));
    allowSyscall(ctx, SCMP_SYS(fspick));
    allowSyscall(ctx, SCMP_SYS(fstat));
    allowSyscall(ctx, SCMP_SYS(fstat64));
    allowSyscall(ctx, SCMP_SYS(fstatat64));
    allowSyscall(ctx, SCMP_SYS(fstatfs));
    allowSyscall(ctx, SCMP_SYS(fstatfs64));
    allowSyscall(ctx, SCMP_SYS(fsync));
    allowSyscall(ctx, SCMP_SYS(ftime));
    allowSyscall(ctx, SCMP_SYS(ftruncate));
    allowSyscall(ctx, SCMP_SYS(ftruncate64));
    allowSyscall(ctx, SCMP_SYS(futex));
    allowSyscall(ctx, SCMP_SYS(futex_requeue));
    allowSyscall(ctx, SCMP_SYS(futex_time64));
    allowSyscall(ctx, SCMP_SYS(futex_wait));
    allowSyscall(ctx, SCMP_SYS(futex_waitv));
    allowSyscall(ctx, SCMP_SYS(futex_wake));
    allowSyscall(ctx, SCMP_SYS(futimesat));
    allowSyscall(ctx, SCMP_SYS(getcpu));
    allowSyscall(ctx, SCMP_SYS(getcwd));
    allowSyscall(ctx, SCMP_SYS(getdents));
    allowSyscall(ctx, SCMP_SYS(getdents64));
    allowSyscall(ctx, SCMP_SYS(getegid));
    allowSyscall(ctx, SCMP_SYS(getegid32));
    allowSyscall(ctx, SCMP_SYS(geteuid));
    allowSyscall(ctx, SCMP_SYS(geteuid32));
    allowSyscall(ctx, SCMP_SYS(getgid));
    allowSyscall(ctx, SCMP_SYS(getgid32));
    allowSyscall(ctx, SCMP_SYS(getgroups));
    allowSyscall(ctx, SCMP_SYS(getgroups32));
    allowSyscall(ctx, SCMP_SYS(getitimer));
    allowSyscall(ctx, SCMP_SYS(get_kernel_syms));
    allowSyscall(ctx, SCMP_SYS(get_mempolicy));
    allowSyscall(ctx, SCMP_SYS(getpeername));
    allowSyscall(ctx, SCMP_SYS(getpgid));
    allowSyscall(ctx, SCMP_SYS(getpgrp));
    allowSyscall(ctx, SCMP_SYS(getpid));
    allowSyscall(ctx, SCMP_SYS(getpmsg));
    allowSyscall(ctx, SCMP_SYS(getppid));
    allowSyscall(ctx, SCMP_SYS(getpriority));
    allowSyscall(ctx, SCMP_SYS(getrandom));
    allowSyscall(ctx, SCMP_SYS(getresgid));
    allowSyscall(ctx, SCMP_SYS(getresgid32));
    allowSyscall(ctx, SCMP_SYS(getresuid));
    allowSyscall(ctx, SCMP_SYS(getresuid32));
    allowSyscall(ctx, SCMP_SYS(getrlimit));
    allowSyscall(ctx, SCMP_SYS(get_robust_list));
    allowSyscall(ctx, SCMP_SYS(getrusage));
    allowSyscall(ctx, SCMP_SYS(getsid));
    allowSyscall(ctx, SCMP_SYS(getsockname));
    allowSyscall(ctx, SCMP_SYS(getsockopt));
    allowSyscall(ctx, SCMP_SYS(get_thread_area));
    allowSyscall(ctx, SCMP_SYS(gettid));
    allowSyscall(ctx, SCMP_SYS(gettimeofday));
    allowSyscall(ctx, SCMP_SYS(get_tls));
    allowSyscall(ctx, SCMP_SYS(getuid));
    allowSyscall(ctx, SCMP_SYS(getuid32));
    allowSyscall(ctx, SCMP_SYS(getxattr));
    allowSyscall(ctx, SCMP_SYS(gtty));
    allowSyscall(ctx, SCMP_SYS(idle));
    allowSyscall(ctx, SCMP_SYS(init_module));
    allowSyscall(ctx, SCMP_SYS(inotify_add_watch));
    allowSyscall(ctx, SCMP_SYS(inotify_init));
    allowSyscall(ctx, SCMP_SYS(inotify_init1));
    allowSyscall(ctx, SCMP_SYS(inotify_rm_watch));
    allowSyscall(ctx, SCMP_SYS(io_cancel));
    allowSyscall(ctx, SCMP_SYS(ioctl));
    allowSyscall(ctx, SCMP_SYS(io_destroy));
    allowSyscall(ctx, SCMP_SYS(io_getevents));
    allowSyscall(ctx, SCMP_SYS(ioperm));
    allowSyscall(ctx, SCMP_SYS(io_pgetevents));
    allowSyscall(ctx, SCMP_SYS(io_pgetevents_time64));
    allowSyscall(ctx, SCMP_SYS(iopl));
    allowSyscall(ctx, SCMP_SYS(ioprio_get));
    allowSyscall(ctx, SCMP_SYS(ioprio_set));
    allowSyscall(ctx, SCMP_SYS(io_setup));
    allowSyscall(ctx, SCMP_SYS(io_submit));
    // skip io_uring_enter (may become dangerous)
    // skip io_uring_register (may become dangerous)
    // skip io_uring_setup (may become dangerous)
    allowSyscall(ctx, SCMP_SYS(ipc));
    allowSyscall(ctx, SCMP_SYS(kcmp));
    allowSyscall(ctx, SCMP_SYS(kexec_file_load));
    allowSyscall(ctx, SCMP_SYS(kexec_load));
    allowSyscall(ctx, SCMP_SYS(keyctl));
    allowSyscall(ctx, SCMP_SYS(kill));
    allowSyscall(ctx, SCMP_SYS(landlock_add_rule));
    allowSyscall(ctx, SCMP_SYS(landlock_create_ruleset));
    allowSyscall(ctx, SCMP_SYS(landlock_restrict_self));
    allowSyscall(ctx, SCMP_SYS(lchown));
    allowSyscall(ctx, SCMP_SYS(lchown32));
    allowSyscall(ctx, SCMP_SYS(lgetxattr));
    allowSyscall(ctx, SCMP_SYS(link));
    allowSyscall(ctx, SCMP_SYS(linkat));
    allowSyscall(ctx, SCMP_SYS(listen));
    allowSyscall(ctx, SCMP_SYS(listxattr));
    allowSyscall(ctx, SCMP_SYS(llistxattr));
    allowSyscall(ctx, SCMP_SYS(_llseek));
    allowSyscall(ctx, SCMP_SYS(lock));
    allowSyscall(ctx, SCMP_SYS(lookup_dcookie));
    allowSyscall(ctx, SCMP_SYS(lremovexattr));
    allowSyscall(ctx, SCMP_SYS(lseek));
    // skip lsetxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(lstat));
    allowSyscall(ctx, SCMP_SYS(lstat64));
    allowSyscall(ctx, SCMP_SYS(madvise));
    allowSyscall(ctx, SCMP_SYS(map_shadow_stack));
    allowSyscall(ctx, SCMP_SYS(mbind));
    allowSyscall(ctx, SCMP_SYS(membarrier));
    allowSyscall(ctx, SCMP_SYS(memfd_create));
    allowSyscall(ctx, SCMP_SYS(memfd_secret));
    allowSyscall(ctx, SCMP_SYS(migrate_pages));
    allowSyscall(ctx, SCMP_SYS(mincore));
    allowSyscall(ctx, SCMP_SYS(mkdir));
    allowSyscall(ctx, SCMP_SYS(mkdirat));
    allowSyscall(ctx, SCMP_SYS(mknod));
    allowSyscall(ctx, SCMP_SYS(mknodat));
    allowSyscall(ctx, SCMP_SYS(mlock));
    allowSyscall(ctx, SCMP_SYS(mlock2));
    allowSyscall(ctx, SCMP_SYS(mlockall));
    allowSyscall(ctx, SCMP_SYS(mmap));
    allowSyscall(ctx, SCMP_SYS(mmap2));
    allowSyscall(ctx, SCMP_SYS(modify_ldt));
    allowSyscall(ctx, SCMP_SYS(mount));
    allowSyscall(ctx, SCMP_SYS(mount_setattr));
    allowSyscall(ctx, SCMP_SYS(move_mount));
    allowSyscall(ctx, SCMP_SYS(move_pages));
    allowSyscall(ctx, SCMP_SYS(mprotect));
    allowSyscall(ctx, SCMP_SYS(mpx));
    allowSyscall(ctx, SCMP_SYS(mq_getsetattr));
    allowSyscall(ctx, SCMP_SYS(mq_notify));
    allowSyscall(ctx, SCMP_SYS(mq_open));
    allowSyscall(ctx, SCMP_SYS(mq_timedreceive));
    allowSyscall(ctx, SCMP_SYS(mq_timedreceive_time64));
    allowSyscall(ctx, SCMP_SYS(mq_timedsend));
    allowSyscall(ctx, SCMP_SYS(mq_timedsend_time64));
    allowSyscall(ctx, SCMP_SYS(mq_unlink));
    allowSyscall(ctx, SCMP_SYS(mremap));
    allowSyscall(ctx, SCMP_SYS(msgctl));
    allowSyscall(ctx, SCMP_SYS(msgget));
    allowSyscall(ctx, SCMP_SYS(msgrcv));
    allowSyscall(ctx, SCMP_SYS(msgsnd));
    allowSyscall(ctx, SCMP_SYS(msync));
    allowSyscall(ctx, SCMP_SYS(multiplexer));
    allowSyscall(ctx, SCMP_SYS(munlock));
    allowSyscall(ctx, SCMP_SYS(munlockall));
    allowSyscall(ctx, SCMP_SYS(munmap));
    allowSyscall(ctx, SCMP_SYS(name_to_handle_at));
    allowSyscall(ctx, SCMP_SYS(nanosleep));
    allowSyscall(ctx, SCMP_SYS(newfstatat));
    allowSyscall(ctx, SCMP_SYS(_newselect));
    allowSyscall(ctx, SCMP_SYS(nfsservctl));
    allowSyscall(ctx, SCMP_SYS(nice));
    allowSyscall(ctx, SCMP_SYS(oldfstat));
    allowSyscall(ctx, SCMP_SYS(oldlstat));
    allowSyscall(ctx, SCMP_SYS(oldolduname));
    allowSyscall(ctx, SCMP_SYS(oldstat));
    allowSyscall(ctx, SCMP_SYS(olduname));
    allowSyscall(ctx, SCMP_SYS(open));
    allowSyscall(ctx, SCMP_SYS(openat));
    allowSyscall(ctx, SCMP_SYS(openat2));
    allowSyscall(ctx, SCMP_SYS(open_by_handle_at));
    allowSyscall(ctx, SCMP_SYS(open_tree));
    allowSyscall(ctx, SCMP_SYS(pause));
    allowSyscall(ctx, SCMP_SYS(pciconfig_iobase));
    allowSyscall(ctx, SCMP_SYS(pciconfig_read));
    allowSyscall(ctx, SCMP_SYS(pciconfig_write));
    allowSyscall(ctx, SCMP_SYS(perf_event_open));
    allowSyscall(ctx, SCMP_SYS(personality));
    allowSyscall(ctx, SCMP_SYS(pidfd_getfd));
    allowSyscall(ctx, SCMP_SYS(pidfd_open));
    allowSyscall(ctx, SCMP_SYS(pidfd_send_signal));
    allowSyscall(ctx, SCMP_SYS(pipe));
    allowSyscall(ctx, SCMP_SYS(pipe2));
    allowSyscall(ctx, SCMP_SYS(pivot_root));
    allowSyscall(ctx, SCMP_SYS(pkey_alloc));
    allowSyscall(ctx, SCMP_SYS(pkey_free));
    allowSyscall(ctx, SCMP_SYS(pkey_mprotect));
    allowSyscall(ctx, SCMP_SYS(poll));
    allowSyscall(ctx, SCMP_SYS(ppoll));
    allowSyscall(ctx, SCMP_SYS(ppoll_time64));
    allowSyscall(ctx, SCMP_SYS(prctl));
    allowSyscall(ctx, SCMP_SYS(pread64));
    allowSyscall(ctx, SCMP_SYS(preadv));
    allowSyscall(ctx, SCMP_SYS(preadv2));
    allowSyscall(ctx, SCMP_SYS(prlimit64));
    allowSyscall(ctx, SCMP_SYS(process_madvise));
    allowSyscall(ctx, SCMP_SYS(process_mrelease));
    allowSyscall(ctx, SCMP_SYS(process_vm_readv));
    allowSyscall(ctx, SCMP_SYS(process_vm_writev));
    allowSyscall(ctx, SCMP_SYS(prof));
    allowSyscall(ctx, SCMP_SYS(profil));
    allowSyscall(ctx, SCMP_SYS(pselect6));
    allowSyscall(ctx, SCMP_SYS(pselect6_time64));
    allowSyscall(ctx, SCMP_SYS(ptrace));
    allowSyscall(ctx, SCMP_SYS(putpmsg));
    allowSyscall(ctx, SCMP_SYS(pwrite64));
    allowSyscall(ctx, SCMP_SYS(pwritev));
    allowSyscall(ctx, SCMP_SYS(pwritev2));
    allowSyscall(ctx, SCMP_SYS(query_module));
    allowSyscall(ctx, SCMP_SYS(quotactl));
    allowSyscall(ctx, SCMP_SYS(quotactl_fd));
    allowSyscall(ctx, SCMP_SYS(read));
    allowSyscall(ctx, SCMP_SYS(readahead));
    allowSyscall(ctx, SCMP_SYS(readdir));
    allowSyscall(ctx, SCMP_SYS(readlink));
    allowSyscall(ctx, SCMP_SYS(readlinkat));
    allowSyscall(ctx, SCMP_SYS(readv));
    allowSyscall(ctx, SCMP_SYS(reboot));
    allowSyscall(ctx, SCMP_SYS(recv));
    allowSyscall(ctx, SCMP_SYS(recvfrom));
    allowSyscall(ctx, SCMP_SYS(recvmmsg));
    allowSyscall(ctx, SCMP_SYS(recvmmsg_time64));
    allowSyscall(ctx, SCMP_SYS(recvmsg));
    allowSyscall(ctx, SCMP_SYS(remap_file_pages));
    allowSyscall(ctx, SCMP_SYS(removexattr));
    allowSyscall(ctx, SCMP_SYS(rename));
    allowSyscall(ctx, SCMP_SYS(renameat));
    allowSyscall(ctx, SCMP_SYS(renameat2));
    allowSyscall(ctx, SCMP_SYS(request_key));
    allowSyscall(ctx, SCMP_SYS(restart_syscall));
    allowSyscall(ctx, SCMP_SYS(riscv_flush_icache));
    allowSyscall(ctx, SCMP_SYS(rmdir));
    allowSyscall(ctx, SCMP_SYS(rseq));
    allowSyscall(ctx, SCMP_SYS(rtas));
    allowSyscall(ctx, SCMP_SYS(rt_sigaction));
    allowSyscall(ctx, SCMP_SYS(rt_sigpending));
    allowSyscall(ctx, SCMP_SYS(rt_sigprocmask));
    allowSyscall(ctx, SCMP_SYS(rt_sigqueueinfo));
    allowSyscall(ctx, SCMP_SYS(rt_sigreturn));
    allowSyscall(ctx, SCMP_SYS(rt_sigsuspend));
    allowSyscall(ctx, SCMP_SYS(rt_sigtimedwait));
    allowSyscall(ctx, SCMP_SYS(rt_sigtimedwait_time64));
    allowSyscall(ctx, SCMP_SYS(rt_tgsigqueueinfo));
    allowSyscall(ctx, SCMP_SYS(s390_guarded_storage));
    allowSyscall(ctx, SCMP_SYS(s390_pci_mmio_read));
    allowSyscall(ctx, SCMP_SYS(s390_pci_mmio_write));
    allowSyscall(ctx, SCMP_SYS(s390_runtime_instr));
    allowSyscall(ctx, SCMP_SYS(s390_sthyi));
    allowSyscall(ctx, SCMP_SYS(sched_getaffinity));
    allowSyscall(ctx, SCMP_SYS(sched_getattr));
    allowSyscall(ctx, SCMP_SYS(sched_getparam));
    allowSyscall(ctx, SCMP_SYS(sched_get_priority_max));
    allowSyscall(ctx, SCMP_SYS(sched_get_priority_min));
    allowSyscall(ctx, SCMP_SYS(sched_getscheduler));
    allowSyscall(ctx, SCMP_SYS(sched_rr_get_interval));
    allowSyscall(ctx, SCMP_SYS(sched_rr_get_interval_time64));
    allowSyscall(ctx, SCMP_SYS(sched_setaffinity));
    allowSyscall(ctx, SCMP_SYS(sched_setattr));
    allowSyscall(ctx, SCMP_SYS(sched_setparam));
    allowSyscall(ctx, SCMP_SYS(sched_setscheduler));
    allowSyscall(ctx, SCMP_SYS(sched_yield));
    allowSyscall(ctx, SCMP_SYS(seccomp));
    allowSyscall(ctx, SCMP_SYS(security));
    allowSyscall(ctx, SCMP_SYS(select));
    allowSyscall(ctx, SCMP_SYS(semctl));
    allowSyscall(ctx, SCMP_SYS(semget));
    allowSyscall(ctx, SCMP_SYS(semop));
    allowSyscall(ctx, SCMP_SYS(semtimedop));
    allowSyscall(ctx, SCMP_SYS(semtimedop_time64));
    allowSyscall(ctx, SCMP_SYS(send));
    allowSyscall(ctx, SCMP_SYS(sendfile));
    allowSyscall(ctx, SCMP_SYS(sendfile64));
    allowSyscall(ctx, SCMP_SYS(sendmmsg));
    allowSyscall(ctx, SCMP_SYS(sendmsg));
    allowSyscall(ctx, SCMP_SYS(sendto));
    allowSyscall(ctx, SCMP_SYS(setdomainname));
    allowSyscall(ctx, SCMP_SYS(setfsgid));
    allowSyscall(ctx, SCMP_SYS(setfsgid32));
    allowSyscall(ctx, SCMP_SYS(setfsuid));
    allowSyscall(ctx, SCMP_SYS(setfsuid32));
    allowSyscall(ctx, SCMP_SYS(setgid));
    allowSyscall(ctx, SCMP_SYS(setgid32));
    allowSyscall(ctx, SCMP_SYS(setgroups));
    allowSyscall(ctx, SCMP_SYS(setgroups32));
    allowSyscall(ctx, SCMP_SYS(sethostname));
    allowSyscall(ctx, SCMP_SYS(setitimer));
    allowSyscall(ctx, SCMP_SYS(set_mempolicy));
    allowSyscall(ctx, SCMP_SYS(set_mempolicy_home_node));
    allowSyscall(ctx, SCMP_SYS(setns));
    allowSyscall(ctx, SCMP_SYS(setpgid));
    allowSyscall(ctx, SCMP_SYS(setpriority));
    allowSyscall(ctx, SCMP_SYS(setregid));
    allowSyscall(ctx, SCMP_SYS(setregid32));
    allowSyscall(ctx, SCMP_SYS(setresgid));
    allowSyscall(ctx, SCMP_SYS(setresgid32));
    allowSyscall(ctx, SCMP_SYS(setresuid));
    allowSyscall(ctx, SCMP_SYS(setresuid32));
    allowSyscall(ctx, SCMP_SYS(setreuid));
    allowSyscall(ctx, SCMP_SYS(setreuid32));
    allowSyscall(ctx, SCMP_SYS(setrlimit));
    allowSyscall(ctx, SCMP_SYS(set_robust_list));
    allowSyscall(ctx, SCMP_SYS(setsid));
    allowSyscall(ctx, SCMP_SYS(setsockopt));
    allowSyscall(ctx, SCMP_SYS(set_thread_area));
    allowSyscall(ctx, SCMP_SYS(set_tid_address));
    allowSyscall(ctx, SCMP_SYS(settimeofday));
    allowSyscall(ctx, SCMP_SYS(set_tls));
    allowSyscall(ctx, SCMP_SYS(setuid));
    allowSyscall(ctx, SCMP_SYS(setuid32));
    // skip setxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(sgetmask));
    allowSyscall(ctx, SCMP_SYS(shmat));
    allowSyscall(ctx, SCMP_SYS(shmctl));
    allowSyscall(ctx, SCMP_SYS(shmdt));
    allowSyscall(ctx, SCMP_SYS(shmget));
    allowSyscall(ctx, SCMP_SYS(shutdown));
    allowSyscall(ctx, SCMP_SYS(sigaction));
    allowSyscall(ctx, SCMP_SYS(sigaltstack));
    allowSyscall(ctx, SCMP_SYS(signal));
    allowSyscall(ctx, SCMP_SYS(signalfd));
    allowSyscall(ctx, SCMP_SYS(signalfd4));
    allowSyscall(ctx, SCMP_SYS(sigpending));
    allowSyscall(ctx, SCMP_SYS(sigprocmask));
    allowSyscall(ctx, SCMP_SYS(sigreturn));
    allowSyscall(ctx, SCMP_SYS(sigsuspend));
    allowSyscall(ctx, SCMP_SYS(socket));
    allowSyscall(ctx, SCMP_SYS(socketcall));
    allowSyscall(ctx, SCMP_SYS(socketpair));
    allowSyscall(ctx, SCMP_SYS(splice));
    allowSyscall(ctx, SCMP_SYS(spu_create));
    allowSyscall(ctx, SCMP_SYS(spu_run));
    allowSyscall(ctx, SCMP_SYS(ssetmask));
    allowSyscall(ctx, SCMP_SYS(stat));
    allowSyscall(ctx, SCMP_SYS(stat64));
    allowSyscall(ctx, SCMP_SYS(statfs));
    allowSyscall(ctx, SCMP_SYS(statfs64));
    allowSyscall(ctx, SCMP_SYS(statx));
    allowSyscall(ctx, SCMP_SYS(stime));
    allowSyscall(ctx, SCMP_SYS(stty));
    allowSyscall(ctx, SCMP_SYS(subpage_prot));
    allowSyscall(ctx, SCMP_SYS(swapcontext));
    allowSyscall(ctx, SCMP_SYS(swapoff));
    allowSyscall(ctx, SCMP_SYS(swapon));
    allowSyscall(ctx, SCMP_SYS(switch_endian));
    allowSyscall(ctx, SCMP_SYS(symlink));
    allowSyscall(ctx, SCMP_SYS(symlinkat));
    allowSyscall(ctx, SCMP_SYS(sync));
    allowSyscall(ctx, SCMP_SYS(sync_file_range));
    allowSyscall(ctx, SCMP_SYS(sync_file_range2));
    allowSyscall(ctx, SCMP_SYS(syncfs));
    allowSyscall(ctx, SCMP_SYS(syscall));
    allowSyscall(ctx, SCMP_SYS(_sysctl));
    allowSyscall(ctx, SCMP_SYS(sys_debug_setcontext));
    allowSyscall(ctx, SCMP_SYS(sysfs));
    allowSyscall(ctx, SCMP_SYS(sysinfo));
    allowSyscall(ctx, SCMP_SYS(syslog));
    allowSyscall(ctx, SCMP_SYS(sysmips));
    allowSyscall(ctx, SCMP_SYS(tee));
    allowSyscall(ctx, SCMP_SYS(tgkill));
    allowSyscall(ctx, SCMP_SYS(time));
    allowSyscall(ctx, SCMP_SYS(timer_create));
    allowSyscall(ctx, SCMP_SYS(timer_delete));
    allowSyscall(ctx, SCMP_SYS(timerfd));
    allowSyscall(ctx, SCMP_SYS(timerfd_create));
    allowSyscall(ctx, SCMP_SYS(timerfd_gettime));
    allowSyscall(ctx, SCMP_SYS(timerfd_gettime64));
    allowSyscall(ctx, SCMP_SYS(timerfd_settime));
    allowSyscall(ctx, SCMP_SYS(timerfd_settime64));
    allowSyscall(ctx, SCMP_SYS(timer_getoverrun));
    allowSyscall(ctx, SCMP_SYS(timer_gettime));
    allowSyscall(ctx, SCMP_SYS(timer_gettime64));
    allowSyscall(ctx, SCMP_SYS(timer_settime));
    allowSyscall(ctx, SCMP_SYS(timer_settime64));
    allowSyscall(ctx, SCMP_SYS(times));
    allowSyscall(ctx, SCMP_SYS(tkill));
    allowSyscall(ctx, SCMP_SYS(truncate));
    allowSyscall(ctx, SCMP_SYS(truncate64));
    allowSyscall(ctx, SCMP_SYS(tuxcall));
    allowSyscall(ctx, SCMP_SYS(ugetrlimit));
    allowSyscall(ctx, SCMP_SYS(ulimit));
    allowSyscall(ctx, SCMP_SYS(umask));
    allowSyscall(ctx, SCMP_SYS(umount));
    allowSyscall(ctx, SCMP_SYS(umount2));
    allowSyscall(ctx, SCMP_SYS(uname));
    allowSyscall(ctx, SCMP_SYS(unlink));
    allowSyscall(ctx, SCMP_SYS(unlinkat));
    allowSyscall(ctx, SCMP_SYS(unshare));
    allowSyscall(ctx, SCMP_SYS(uselib));
    allowSyscall(ctx, SCMP_SYS(userfaultfd));
    allowSyscall(ctx, SCMP_SYS(usr26));
    allowSyscall(ctx, SCMP_SYS(usr32));
    allowSyscall(ctx, SCMP_SYS(ustat));
    allowSyscall(ctx, SCMP_SYS(utime));
    allowSyscall(ctx, SCMP_SYS(utimensat));
    allowSyscall(ctx, SCMP_SYS(utimensat_time64));
    allowSyscall(ctx, SCMP_SYS(utimes));
    allowSyscall(ctx, SCMP_SYS(vfork));
    allowSyscall(ctx, SCMP_SYS(vhangup));
    allowSyscall(ctx, SCMP_SYS(vm86));
    allowSyscall(ctx, SCMP_SYS(vm86old));
    allowSyscall(ctx, SCMP_SYS(vmsplice));
    allowSyscall(ctx, SCMP_SYS(vserver));
    allowSyscall(ctx, SCMP_SYS(wait4));
    allowSyscall(ctx, SCMP_SYS(waitid));
    allowSyscall(ctx, SCMP_SYS(waitpid));
    allowSyscall(ctx, SCMP_SYS(write));
    allowSyscall(ctx, SCMP_SYS(writev));
    // END extract-syscalls

    // chmod family: prevent adding setuid/setgid bits to existing files.
    // The Nix store does not support setuid/setgid, and even their temporary creation can weaken the security of the sandbox.
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(chmod), 1);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmod), 1);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmodat), 2);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmodat2), 2);

    // setxattr family: prevent creation of extended attributes or ACLs.
    // Not all filesystems support them, and they're incompatible with the NAR format.
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0
        || seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
    {
        throw SysError("unable to add seccomp rule");
    }

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

void LinuxLocalDerivationGoal::setupSyscallFilter()
{
    // Set the NO_NEW_PRIVS prctl flag.
    // This both makes loading seccomp filters work for unprivileged users,
    // and is an additional security measure in its own right.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
        throw SysError("PR_SET_NO_NEW_PRIVS failed");
#if HAVE_SECCOMP
    const auto &seccompBPF = getSyscallFilter();
    assert(seccompBPF.size() <= std::numeric_limits<unsigned short>::max());
    struct sock_fprog fprog = {
        .len = static_cast<unsigned short>(seccompBPF.size()),
        // the kernel does not actually write to the filter
        .filter = const_cast<struct sock_filter *>(seccompBPF.data()),
    };
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &fprog) != 0)
        throw SysError("unable to load seccomp BPF program");
#endif
}

void LinuxLocalDerivationGoal::prepareSandbox()
{
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

bool LinuxLocalDerivationGoal::prepareChildSetup()
{
    setupSyscallFilter();

    KJ_DEFER(setPersonality(drv->platform));

    if (!useChroot) {
        return true;
    }

    if (privateNetwork()) {

        /* Initialise the loopback interface. */
        AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
        if (!fd) {
            throw SysError("cannot open IP socket");
        }

        struct ifreq ifr;
        strcpy(ifr.ifr_name, "lo");
        ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
        if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1) {
            throw SysError("cannot set loopback interface flags");
        }
    }

    /* Set the hostname etc. to fixed values. */
    char hostname[] = "localhost";
    if (sethostname(hostname, sizeof(hostname)) == -1) {
        throw SysError("cannot set host name");
    }
    char domainname[] = "(none)"; // kernel default
    if (setdomainname(domainname, sizeof(domainname)) == -1) {
        throw SysError("cannot set domain name");
    }

    /* Make all filesystems private.  This is necessary
       because subtrees may have been mounted as "shared"
       (MS_SHARED).  (Systemd does this, for instance.)  Even
       though we have a private mount namespace, mounting
       filesystems on top of a shared subtree still propagates
       outside of the namespace.  Making a subtree private is
       local to the namespace, though, so setting MS_PRIVATE
       does not affect the outside world. */
    if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1) {
        throw SysError("unable to make '/' private");
    }

    /* Bind-mount chroot directory to itself, to treat it as a
       different filesystem from /, as needed for pivot_root. */
    if (sys::mount(chrootRootDir, chrootRootDir, "", MS_BIND, 0) == -1) {
        throw SysError("unable to bind mount '%1%'", chrootRootDir);
    }

    /* Bind-mount the sandbox's Nix store onto itself so that
       we can mark it as a "shared" subtree, allowing bind
       mounts made in *this* mount namespace to be propagated
       into the child namespace created by the
       unshare(CLONE_NEWNS) call below.

       Marking chrootRootDir as MS_SHARED causes pivot_root()
       to fail with EINVAL. Don't know why. */
    Path chrootStoreDir = chrootRootDir + worker.store.config().storeDir;

    if (sys::mount(chrootStoreDir, chrootStoreDir, "", MS_BIND, 0) == -1) {
        throw SysError("unable to bind mount the Nix store", chrootStoreDir);
    }

    if (sys::mount("", chrootStoreDir, "", MS_SHARED, 0) == -1) {
        throw SysError("unable to make '%s' shared", chrootStoreDir);
    }

    /* Set up a nearly empty /dev, unless the user asked to
       bind-mount the host /dev. */
    Strings ss;
    if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
        createDirs(chrootRootDir + "/dev/shm");
        createDirs(chrootRootDir + "/dev/pts");
        ss.push_back("/dev/full");
        if (worker.store.config().systemFeatures.get().count("kvm") && pathExists("/dev/kvm")) {
            ss.push_back("/dev/kvm");
        }
        ss.push_back("/dev/null");
        ss.push_back("/dev/random");
        ss.push_back("/dev/tty");
        ss.push_back("/dev/urandom");
        ss.push_back("/dev/zero");
        createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
        createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
        createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
        createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
    }

    for (auto & i : ss) {
        pathsInChroot.emplace(i, i);
    }

    /* Bind-mount all the directories from the "host"
       filesystem that we want in the chroot
       environment. */
    for (auto & i : pathsInChroot) {
        if (i.second.source == "/proc") {
            continue; // backwards compatibility
        }

#if HAVE_EMBEDDED_SANDBOX_SHELL
        if (i.second.source == "__embedded_sandbox_shell__") {
            static unsigned char sh[] = {
#include "embedded-sandbox-shell.gen.hh"
                    };
            auto dst = chrootRootDir + i.first;
            createDirs(dirOf(dst));
            writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
            chmodPath(dst, 0555);
        } else
#endif
            bindPath(i.second.source, chrootRootDir + i.first, i.second.optional);
    }

    /* Bind a new instance of procfs on /proc. */
    createDirs(chrootRootDir + "/proc");
    if (sys::mount("none", chrootRootDir + "/proc", "proc", 0, 0) == -1) {
        throw SysError("mounting /proc");
    }

    /* Mount sysfs on /sys. */
    if (buildUser && buildUser->getUIDCount() != 1) {
        createDirs(chrootRootDir + "/sys");
        if (sys::mount("none", chrootRootDir + "/sys", "sysfs", 0, 0) == -1) {
            throw SysError("mounting /sys");
        }
    }

    /* Mount a new tmpfs on /dev/shm to ensure that whatever
       the builder puts in /dev/shm is cleaned up automatically. */
    if (pathExists("/dev/shm")
        && sys::mount(
               "none", chrootRootDir + "/dev/shm", "tmpfs", 0, fmt("size=%s", settings.sandboxShmSize).c_str()
           ) == -1)
    {
        throw SysError("mounting /dev/shm");
    }

    /* Mount a new devpts on /dev/pts.  Note that this
       requires the kernel to be compiled with
       CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
       if /dev/ptx/ptmx exists). */
    if (pathExists("/dev/pts/ptmx") && !pathExists(chrootRootDir + "/dev/ptmx")
        && !pathsInChroot.count("/dev/pts"))
    {
        if (sys::mount("none", (chrootRootDir + "/dev/pts"), "devpts", 0, "newinstance,mode=0620") == 0) {
            createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

            /* Make sure /dev/pts/ptmx is world-writable.  With some
               Linux versions, it is created with permissions 0.  */
            chmodPath(chrootRootDir + "/dev/pts/ptmx", 0666);
        } else {
            if (errno != EINVAL) {
                throw SysError("mounting /dev/pts");
            }
            bindPath("/dev/pts", chrootRootDir + "/dev/pts");
            bindPath("/dev/ptmx", chrootRootDir + "/dev/ptmx");
        }
    }

    /* Make /etc unwritable */
    if (!parsedDrv->useUidRange()) {
        chmodPath(chrootRootDir + "/etc", 0555);
    }

    /* The comment below is now outdated. Recursive Nix has been removed.
     * So there's no need to make path appear in the sandbox.
     * TODO(Raito): cleanup before a merge.
     */
    /* Unshare this mount namespace. This is necessary because
       pivot_root() below changes the root of the mount
       namespace. This means that the call to setns() in
       addDependency() would hide the host's filesystem,
       making it impossible to bind-mount paths from the host
       Nix store into the sandbox. Therefore, we save the
       pre-pivot_root namespace in
       sandboxMountNamespace. Since we made /nix/store a
       shared subtree above, this allows addDependency() to
       make paths appear in the sandbox. */
    if (unshare(CLONE_NEWNS) == -1) {
        throw SysError("unsharing mount namespace");
    }

    /* Creating a new cgroup namespace is independent of whether we enabled the cgroup experimental feature.
     * We always create a new cgroup namespace from a sandboxing perspective. */
    /* Unshare the cgroup namespace. This means
       /proc/self/cgroup will show the child's cgroup as '/'
       rather than whatever it is in the parent. */
    if (unshare(CLONE_NEWCGROUP) == -1) {
        throw SysError("unsharing cgroup namespace");
    }

    /* Do the chroot(). */
    if (sys::chdir(chrootRootDir) == -1) {
        throw SysError("cannot change directory to '%1%'", chrootRootDir);
    }

    if (mkdir("real-root", 0) == -1) {
        throw SysError("cannot create real-root directory");
    }

    if (syscall(SYS_pivot_root, ".", "real-root") == -1) {
        throw SysError("cannot pivot old root directory onto '%1%'", (chrootRootDir + "/real-root"));
    }

    if (chroot(".") == -1) {
        throw SysError("cannot change root directory to '%1%'", chrootRootDir);
    }

    if (umount2("real-root", MNT_DETACH) == -1) {
        throw SysError("cannot unmount real root filesystem");
    }

    if (rmdir("real-root") == -1) {
        throw SysError("cannot remove real-root directory");
    }

    /* Switch to the sandbox uid/gid in the user namespace,
       which corresponds to the build user or calling user in
       the parent namespace. */
    if (setgid(sandboxGid()) == -1) {
        throw SysError("setgid failed");
    }
    if (setuid(sandboxUid()) == -1) {
        throw SysError("setuid failed");
    }

    if (runPasta()) {
        // wait for the pasta interface to appear. pasta can't signal us when
        // it's done setting up the namespace, so we have to wait for a while
        AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
        if (!fd) {
            throw SysError("cannot open IP socket");
        }

        struct ifreq ifr;
        strcpy(ifr.ifr_name, LinuxLocalDerivationGoal::PASTA_NS_IFNAME);
        // wait two minutes for the interface to appear. if it does not do so
        // we are either grossly overloaded, or pasta startup failed somehow.
        static constexpr int SINGLE_WAIT_US = 1000;
        static constexpr int TOTAL_WAIT_US = 120'000'000;
        for (unsigned tries = 0;; tries++) {
            if (tries > TOTAL_WAIT_US / SINGLE_WAIT_US) {
                throw Error(
                    "sandbox network setup timed out, please check daemon logs for "
                    "possible error output."
                );
            } else if (ioctl(fd.get(), SIOCGIFFLAGS, &ifr) == 0) {
                if ((ifr.ifr_ifru.ifru_flags & IFF_UP) != 0) {
                    break;
                }
            } else if (errno == ENODEV) {
                usleep(SINGLE_WAIT_US);
            } else {
                throw SysError("cannot get loopback interface flags");
            }
        }
    }

    return false;
}

Pid LinuxLocalDerivationGoal::startChild(
    const Path & builder, const Strings & envStrs, const Strings & args, AutoCloseFD logPTY
)
{
#if HAVE_SECCOMP
    // Our seccomp filter program is surprisingly expensive to compile (~10ms).
    // For this reason, we precompile it once and then cache it.
    // This has to be done in the parent so that all builds get to use the same cache.
    getSyscallFilter();
#endif

    // If we're not sandboxing no need to faff about, use the fallback
    if (!useChroot) {
        return LocalDerivationGoal::startChild(builder, envStrs, args, std::move(logPTY));
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
                    raise(SIGSTOP);
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

    Pid pid = inVFork(/* flags*/ 0, [&]() {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
            throw SysError("setting death signal");

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
            if (errno != EPERM)
                throw SysError("setgroups failed");
            if (settings.requireDropSupplementaryGroups)
                throw Error("setgroups failed. Set the require-drop-supplementary-groups option to false to skip this step.");
        }

        if (userns && setns(userns.get(), 0)) {
            throw SysError("setns(userNS)");
        }
        if (netns && setns(netns.get(), 0)) {
            throw SysError("setns(netNS)");
        }

        ProcessOptions options;
        options.cloneFlags =
            CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;

        return startProcess(
            [&]() {
                if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) {
                    throw SysError("setting death signal");
                }
                runChild(builder, envStrs, args);
            },
            options
        );
    });

    if (runPasta()) {
        // Bring up pasta, for handling FOD networking. We don't let it daemonize
        // itself for process managements reasons and kill it manually when done.

        AutoCloseFD netns(sys::open(fmt("/proc/%i/ns/net", pid.get()), O_RDONLY | O_CLOEXEC));
        if (!netns) {
            throw SysError("failed to open netns");
        }

        AutoCloseFD userns;
        if (worker.namespaces.user) {
            userns =
                AutoCloseFD(sys::open(fmt("/proc/%i/ns/user", pid.get()), O_RDONLY | O_CLOEXEC));
            if (!userns) {
                throw SysError("failed to open userns");
            }
        }

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

    return pid;
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
