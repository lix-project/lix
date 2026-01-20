#include "c-calls.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"

#include <kj/common.h>
#include <ranges>
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

        if (sys::chdir(savedCwd) == -1) {
            throw SysError("restoring cwd");
        }
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

kj::Promise<Result<AvailableNamespaces>> queryAvailableNamespaces()
try {
    AvailableNamespaces result{};

    auto helper = runHelper("check-namespace-support", {.captureStdout = true});
    KJ_DEFER(helper.waitAndCheck());
    const auto results = tokenizeString<Strings>(TRY_AWAIT(helper.getStdout()->drain()), "\n");

    for (auto line : results) {
        if (line == "user") {
            result.user = true;
        } else if (line.starts_with("user ")) {
            printTaggedWarning("user namespaces do not work on this system: %s", line.substr(5));
            diagnoseUserNamespaces();
        } else if (line == "mount-pid") {
            result.mountAndPid = true;
        } else if (line.starts_with("mount-pid")) {
            debug("mount namespaces do not work on this system: %s", line.substr(9));
        } else {
            throw Error("unexpected namespace check status: %s", line);
        }
    }

    co_return result;
} catch (...) {
    co_return result::current_exception();
}
#else
kj::Promise<Result<AvailableNamespaces>> queryAvailableNamespaces()
try {
    return {AvailableNamespaces{}};
} catch (...) {
    return {result::current_exception()};
}
#endif
}
