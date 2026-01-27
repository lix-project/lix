#include "c-calls.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace nix {

std::string getUserName()
{
    auto pw = getpwuid(geteuid());
    std::string name = pw ? pw->pw_name : getEnv("USER").value_or("");
    if (name.empty())
        throw Error("cannot figure out user name");
    return name;
}

static std::optional<Path> tryGetHomeOf(uid_t userId)
{
    std::vector<char> buf(16384);
    struct passwd pwbuf;
    struct passwd * pw;
    if (getpwuid_r(userId, &pwbuf, buf.data(), buf.size(), &pw) != 0 || !pw || !pw->pw_dir || !pw->pw_dir[0])
    {
        return std::nullopt;
    }
    return pw->pw_dir;
}

std::optional<Path> tryGetHome()
{
    static std::optional<Path> homeDir = []() {
        std::optional<std::string> unownedUserHomeDir = {};
        auto homeDir = getEnv("HOME");
        if (homeDir) {
            // Only use `$HOME` if it exists and is owned by the current user.
            struct stat st;
            int result = sys::stat(*homeDir, &st);
            if (result != 0) {
                if (errno != ENOENT) {
                    printTaggedWarning(
                        "couldn't stat $HOME ('%s') for reason other than not existing ('%d'), "
                        "falling back to the one defined in the 'passwd' file",
                        *homeDir,
                        errno
                    );
                    homeDir.reset();
                }
            } else if (st.st_uid != geteuid()) {
                unownedUserHomeDir.swap(homeDir);
            }
        }
        if (!homeDir) {
            homeDir = tryGetHomeOf(geteuid());
            if (homeDir && unownedUserHomeDir && unownedUserHomeDir != homeDir) {
                printTaggedWarning(
                    "$HOME ('%s') is not owned by you, falling back to the one defined in the "
                    "'passwd' file ('%s')",
                    *unownedUserHomeDir,
                    *homeDir
                );
            }
        }
        return homeDir;
    }();
    return homeDir;
}

Path getHome()
{
    if (auto home = tryGetHome()) {
        return std::move(*home);
    } else {
        throw Error("cannot determine user's home directory");
    }
}

Path getCacheDir()
{
    // We follow systemd semantics here:
    // https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html#RuntimeDirectory=
    static auto cacheDir = [] {
        auto userCacheDir = getEnv("XDG_CACHE_HOME");
        auto serviceCacheDir = getEnv("CACHE_DIRECTORY");

        if (serviceCacheDir) {
            return *serviceCacheDir;
        }

        if (userCacheDir) {
            return *userCacheDir;
        }

        return getHome() + "/.cache";
    }();

    return cacheDir;
}


Path getConfigDir()
{
    auto configDir = getEnv("XDG_CONFIG_HOME");
    return configDir ? *configDir : getHome() + "/.config";
}

std::vector<Path> getConfigDirs()
{
    auto configDirs = getEnv("XDG_CONFIG_DIRS").value_or("/etc/xdg");
    std::vector<Path> result = tokenizeString<std::vector<std::string>>(configDirs, ":");
    if (auto configHome = getEnv("XDG_CONFIG_HOME")) {
        result.insert(result.begin(), *configHome);
    } else if (auto userHome = tryGetHome()) {
        result.insert(result.begin(), *userHome + "/.config");
    }
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

}
