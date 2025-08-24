#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"

#include <sys/mount.h>

#if __linux__
# include <mutex>
# include <sys/resource.h>
#endif

namespace nix {

#if __linux__
static AutoCloseFD fdSavedMountNamespace;
static AutoCloseFD fdSavedRoot;
#endif

void saveMountNamespace()
{
#if __linux__
    static std::once_flag done;
    std::call_once(done, []() {
        fdSavedMountNamespace = AutoCloseFD{open("/proc/self/ns/mnt", O_RDONLY)};
        if (!fdSavedMountNamespace)
            throw SysError("saving parent mount namespace");

        fdSavedRoot = AutoCloseFD{open("/proc/self/root", O_RDONLY)};
    });
#endif
}

void restoreMountNamespace()
{
#if __linux__
    try {
        auto savedCwd = absPath(".");

        if (fdSavedMountNamespace && setns(fdSavedMountNamespace.get(), CLONE_NEWNS) == -1)
            throw SysError("restoring parent mount namespace");

        if (fdSavedRoot) {
            if (fchdir(fdSavedRoot.get()))
                throw SysError("chdir into saved root");
            if (chroot("."))
                throw SysError("chroot into saved root");
        }

        if (chdir(savedCwd.c_str()) == -1)
            throw SysError("restoring cwd");
    } catch (Error & e) {
        debug("%1%", Uncolored(e.msg()));
    }
#endif
}

void unshareFilesystem()
{
#ifdef __linux__
    if (unshare(CLONE_FS) != 0 && errno != EPERM)
        throw SysError("unsharing filesystem state in download thread");
#endif
}


#if __linux__

static void diagnoseUserNamespaces()
{
    if (!pathExists("/proc/self/ns/user")) {
        printTaggedWarning(
            "'/proc/self/ns/user' does not exist; your kernel was likely built without "
            "CONFIG_USER_NS=y"
        );
    }

    Path maxUserNamespaces = "/proc/sys/user/max_user_namespaces";
    if (!pathExists(maxUserNamespaces) ||
        trim(readFile(maxUserNamespaces)) == "0")
    {
        printTaggedWarning(
            "user namespaces appear to be disabled; check '/proc/sys/user/max_user_namespaces'"
        );
    }

    Path procSysKernelUnprivilegedUsernsClone = "/proc/sys/kernel/unprivileged_userns_clone";
    if (pathExists(procSysKernelUnprivilegedUsernsClone)
        && trim(readFile(procSysKernelUnprivilegedUsernsClone)) == "0")
    {
        printTaggedWarning(
            "user namespaces appear to be disabled for unprivileged users; check "
            "'/proc/sys/kernel/unprivileged_userns_clone'"
        );
    }
}

bool userNamespacesSupported()
{
    static auto res = [&]() -> bool
    {
        try {
            Pid pid = startProcess([&]() { _exit(0); }, {.cloneFlags = CLONE_NEWUSER});

            auto r = pid.wait();
            assert(!r);
        } catch (SysError & e) {
            printTaggedWarning("user namespaces do not work on this system: %s", e.msg());
            diagnoseUserNamespaces();
            return false;
        }

        return true;
    }();
    return res;
}

bool mountAndPidNamespacesSupported()
{
    static auto res = [&]() -> bool
    {
        try {

            Pid pid = startProcess([&]() {
                /* Make sure we don't remount the parent's /proc. */
                if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                    _exit(1);

                /* Test whether we can remount /proc. The kernel disallows
                   this if /proc is not fully visible, i.e. if there are
                   filesystems mounted on top of files inside /proc.  See
                   https://lore.kernel.org/lkml/87tvsrjai0.fsf@xmission.com/T/. */
                if (mount("none", "/proc", "proc", 0, 0) == -1)
                    _exit(2);

                _exit(0);
            }, {
                .cloneFlags = CLONE_NEWNS | CLONE_NEWPID | (userNamespacesSupported() ? CLONE_NEWUSER : 0)
            });

            if (pid.wait()) {
                debug("PID namespaces do not work on this system: cannot remount /proc");
                return false;
            }

        } catch (SysError & e) {
            debug("mount namespaces do not work on this system: %s", e.msg());
            return false;
        }

        return true;
    }();
    return res;
}

#endif

}
