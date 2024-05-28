#include "util.hh"
#include "processes.hh"
#include "strings.hh"
#include "current-process.hh"

#include "sync.hh"
#include "finally.hh"
#include "serialise.hh"
#include "cgroup.hh"
#include "signals.hh"
#include "environment-variables.hh"
#include "file-system.hh"

#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/syscall.h>
#include <mach-o/dyld.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include <cmath>
#endif


#ifdef NDEBUG
#error "Lix may not be built with assertions disabled (i.e. with -DNDEBUG)."
#endif

namespace nix {


std::string getUserName()
{
    auto pw = getpwuid(geteuid());
    std::string name = pw ? pw->pw_name : getEnv("USER").value_or("");
    if (name.empty())
        throw Error("cannot figure out user name");
    return name;
}

Path getHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0
        || !pw || !pw->pw_dir || !pw->pw_dir[0])
        throw Error("cannot determine user's home directory");
    return pw->pw_dir;
}

Path getHome()
{
    static Path homeDir = []()
    {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use $HOME if doesn't exist or is owned by the current user.
            struct stat st;
            int result = stat(homeDir->c_str(), &st);
            if (result != 0) {
                if (errno != ENOENT) {
                    warn("couldn't stat $HOME ('%s') for reason other than not existing ('%d'), falling back to the one defined in the 'passwd' file", *homeDir, errno);
                    homeDir.reset();
                }
            } else if (st.st_uid != geteuid()) {
                unownedUserHomeDir.swap(homeDir);
            }
        }
        if (!homeDir) {
            homeDir = getHomeOf(geteuid());
            if (unownedUserHomeDir.has_value() && unownedUserHomeDir != homeDir) {
                warn("$HOME ('%s') is not owned by you, falling back to the one defined in the 'passwd' file ('%s')", *unownedUserHomeDir, *homeDir);
            }
        }
        return *homeDir;
    }();
    return homeDir;
}


Path getCacheDir()
{
    auto cacheDir = getEnv("XDG_CACHE_HOME");
    return cacheDir ? *cacheDir : getHome() + "/.cache";
}


Path getConfigDir()
{
    auto configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() + "/.config";
}

std::vector<Path> getConfigDirs()
{
    Path configHome = getConfigDir();
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    result.insert(result.begin(), configHome);
    return result;
}


Path getDataDir()
{
    auto dataDir = getEnv("XDG_DATA_HOME");
    return dataDir ? *dataDir : getHome() + "/.local/share";
}

Path getStateDir()
{
    auto stateDir = getEnv("XDG_STATE_HOME");
    return stateDir ? *stateDir : getHome() + "/.local/state";
}

Path createNixStateDir()
{
    Path dir = getStateDir() + "/nix";
    createDirs(dir);
    return dir;
}





//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////





void ignoreException(Verbosity lvl)
{
    /* Make sure no exceptions leave this function.
       printError() also throws when remote is closed. */
    try {
        try {
            throw;
        } catch (std::exception & e) {
            printMsg(lvl, "error (ignored): %1%", e.what());
        }
    } catch (...) { }
}


//////////////////////////////////////////////////////////////////////



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
        debug(e.msg());
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


}
