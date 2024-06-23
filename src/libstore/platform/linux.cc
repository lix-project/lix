#include "cgroup.hh"
#include "gc-store.hh"
#include "signals.hh"
#include "platform/linux.hh"
#include "regex.hh"

#include <regex>

namespace nix {
static RegisterStoreImplementation<LinuxLocalStore, LocalStoreConfig> regLocalStore;

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

void LinuxLocalStore::findPlatformRoots(UncheckedRoots & unchecked)
{
    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        auto digitsRegex = std::regex(R"(^\d+$)");
        auto mapRegex = std::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = regex::storePathRegex(storeDir);
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
}

void LinuxLocalDerivationGoal::killSandbox(bool getStats)
{
    if (cgroup) {
        auto stats = destroyCgroup(*cgroup);
        if (getStats) {
            buildResult.cpuUser = stats.cpuUser;
            buildResult.cpuSystem = stats.cpuSystem;
        }
    } else {
        LocalDerivationGoal::killSandbox(getStats);
    }
}
}
