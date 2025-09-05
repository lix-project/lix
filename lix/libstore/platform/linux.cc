#include "lix/libstore/build/worker.hh"
#include "lix/libutil/cgroup.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/finally.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/platform/linux.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"

#include <csignal>
#include <cstdlib>
#include <grp.h>
#include <memory>
#include <regex>
#include <sys/prctl.h>

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
}

void registerLocalStore() {
    StoreImplementations::add<LinuxLocalStore, LocalStoreConfig>();
}

static void readProcLink(const std::string & file, UncheckedRoots & roots)
{
    constexpr auto bufsiz = PATH_MAX;
    char buf[bufsiz];
    auto res = readlink(file.c_str(), buf, bufsiz);
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
                    auto fdDir = AutoCloseDir(opendir(fdStr.c_str()));
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
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

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
    if (mkdir(chrootRootDir.c_str(), buildUser && buildUser->getUIDCount() != 1 ? 0755 : 0750) == -1)
        throw SysError("cannot create '%1%'", chrootRootDir);

    if (buildUser && chown(chrootRootDir.c_str(), buildUser->getUIDCount() != 1 ? buildUser->getUID() : 0, buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", chrootRootDir);

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

    if (buildUser && chown(chrootStoreDir.c_str(), 0, buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", chrootStoreDir);

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
            fmt("nix-build-uid-%d", buildUser->getUID()),
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
    if (!runPasta) {
        return fromHost;
    }

    static constexpr auto flags = std::regex::ECMAScript | std::regex::multiline;
    static auto lineRegex = regex::parse("^nameserver\\s.*$", flags);
    static auto v4Regex = regex::parse("^nameserver\\s+\\d{1,3}\\.", flags);
    static auto v6Regex = regex::parse("^nameserver.*:", flags);
    std::string nsInSandbox = "\n";
    if (std::regex_search(fromHost, v4Regex)) {
        nsInSandbox += fmt("nameserver %s\n", PASTA_HOST_IPV4);
    }
    if (std::regex_search(fromHost, v6Regex)) {
        nsInSandbox += fmt("nameserver %s\n", PASTA_HOST_IPV6);
    }
    return std::regex_replace(fromHost, lineRegex, "") + nsInSandbox;
}

Pid LinuxLocalDerivationGoal::startChild(std::function<void()> openSlave)
{
#if HAVE_SECCOMP
    // Our seccomp filter program is surprisingly expensive to compile (~10ms).
    // For this reason, we precompile it once and then cache it.
    // This has to be done in the parent so that all builds get to use the same cache.
    getSyscallFilter();
#endif

    // If we're not sandboxing no need to faff about, use the fallback
    if (!useChroot) {
        return LocalDerivationGoal::startChild(openSlave);
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

    if (derivationType->isSandboxed())
        privateNetwork = true;

    // don't launch pasta unless we have a tun device. in a build sandbox we
    // commonly do not, and trying to run pasta anyway naturally won't work.
    runPasta = !privateNetwork && settings.pastaPath != "" && pathExists("/dev/net/tun");

    userNamespaceSync.create();

    Pipe sendPid;
    sendPid.create();

    Pid helper = startProcess([&]() {
        sendPid.readSide.close();

        /* We need to open the slave early, before
           CLONE_NEWUSER. Otherwise we get EPERM when running as
           root. */
        openSlave();

        /* Drop additional groups here because we can't do it
           after we've created the new user namespace. */
        if (setgroups(0, 0) == -1) {
            if (errno != EPERM)
                throw SysError("setgroups failed");
            if (settings.requireDropSupplementaryGroups)
                throw Error("setgroups failed. Set the require-drop-supplementary-groups option to false to skip this step.");
        }

        ProcessOptions options;
        options.cloneFlags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_PARENT | SIGCHLD;
        // we always want to create a new network namespace for pasta, even when
        // we can't actually run it. not doing so hides bugs and impairs purity.
        if (settings.pastaPath != "" || privateNetwork)
            options.cloneFlags |= CLONE_NEWNET;
        if (usingUserNamespace)
            options.cloneFlags |= CLONE_NEWUSER;

        pid_t child = startProcess([&]() { runChild(); }, options).release();

        writeFull(sendPid.writeSide.get(), fmt("%d\n", child));
        _exit(0);
    });

    sendPid.writeSide.close();

    if (helper.wait() != 0)
        throw Error("unable to start build process");

    userNamespaceSync.readSide.reset();

    /* Close the write side to prevent runChild() from hanging
       reading from this. */
    Finally cleanup([&]() {
        userNamespaceSync.writeSide.reset();
    });

    auto ss = tokenizeString<std::vector<std::string>>(readLine(sendPid.readSide.get()));
    assert(ss.size() == 1);
    Pid pid = Pid{string2Int<pid_t>(ss[0]).value()};

    if (usingUserNamespace) {
        /* Set the UID/GID mapping of the builder's user namespace
           such that the sandbox user maps to the build user, or to
           the calling user (if build users are disabled). */
        uid_t hostUid = buildUser ? buildUser->getUID() : getuid();
        uid_t hostGid = buildUser ? buildUser->getGID() : getgid();
        uid_t nrIds = buildUser ? buildUser->getUIDCount() : 1;

        writeFile("/proc/" + std::to_string(pid.get()) + "/uid_map",
            fmt("%d %d %d", sandboxUid(), hostUid, nrIds));

        if (!buildUser || buildUser->getUIDCount() == 1)
            writeFile("/proc/" + std::to_string(pid.get()) + "/setgroups", "deny");

        writeFile("/proc/" + std::to_string(pid.get()) + "/gid_map",
            fmt("%d %d %d", sandboxGid(), hostGid, nrIds));
    } else {
        debug("note: not using a user namespace");
    }

    /* Now that we now the sandbox uid, we can write
       /etc/passwd. */
    writeFile(chrootRootDir + "/etc/passwd", fmt(
            "root:x:0:0:Nix build user:%3%:/noshell\n"
            "nixbld:x:%1%:%2%:Nix build user:%3%:/noshell\n"
            "nobody:x:65534:65534:Nobody:/:/noshell\n",
            sandboxUid(), sandboxGid(), settings.sandboxBuildDir));

    /* Declare the build user's group so that programs get a consistent
       view of the system (e.g., "id -gn"). */
    writeFile(chrootRootDir + "/etc/group",
        fmt("root:x:0:\n"
            "nixbld:!:%1%:\n"
            "nogroup:x:65534:\n", sandboxGid()));

    /* Migrate the child inside the available control group. */
    if (context.cgroup) {
        context.cgroup->adoptProcess(pid.get());
    }

    /* Signal the builder that we've updated its user namespace. */
    writeFull(userNamespaceSync.writeSide.get(), "1");

    if (runPasta) {
        // Bring up pasta, for handling FOD networking. We don't let it daemonize
        // itself for process managements reasons and kill it manually when done.

        // TODO add a new sandbox mode flag to disable all or parts of this?
        Strings args = {
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
            "--netns", "/proc/self/fd/0",
            // clang-format on
        };

        AutoCloseFD netns(open(fmt("/proc/%i/ns/net", pid.get()).c_str(), O_RDONLY | O_CLOEXEC));
        if (!netns) {
            throw SysError("failed to open netns");
        }

        AutoCloseFD userns;
        if (usingUserNamespace) {
            userns =
                AutoCloseFD(open(fmt("/proc/%i/ns/user", pid.get()).c_str(), O_RDONLY | O_CLOEXEC));
            if (!userns) {
                throw SysError("failed to open userns");
            }
            args.push_back("--userns");
            args.push_back("/proc/self/fd/1");
        }

        // FIXME ideally we want a notification when pasta exits, but we cannot do
        // this at present. without such support we need to busy-wait for pasta to
        // set up the namespace completely and time out after a while for the case
        // of pasta launch failures. pasta logs go to syslog only for now as well.
        pastaPid = runProgram2({
            .program = settings.pastaPath,
            .args = args,
            .uid = useBuildUsers() ? std::optional(buildUser->getUID()) : std::nullopt,
            .gid = useBuildUsers() ? std::optional(buildUser->getGID()) : std::nullopt,
            // TODO these redirections are crimes. pasta closes all non-stdio file
            // descriptors very early and lacks fd arguments for the namespaces we
            // want it to join. we cannot have pasta join the namespaces via pids;
            // doing so requires capabilities which pasta *also* drops very early.
            .redirections =
                {
                    {.dup = 0, .from = netns.get()},
                    {.dup = 1, .from = userns ? userns.get() : 1},
                },
            .caps = getuid() == 0 ? std::set<long>{CAP_SYS_ADMIN, CAP_NET_BIND_SERVICE}
                                  : std::set<long>{},
        });
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
