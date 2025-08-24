#include "lix/libutil/current-process.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/cgroup.hh"
#include <math.h>

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

#if __linux__
# include <sys/resource.h>
#endif

#if __FreeBSD__
# include <sys/param.h>
# include <sys/sysctl.h>
#endif

#include <sys/mount.h>

namespace nix {

unsigned int getMaxCPU()
{
    #if __linux__
    try {
        auto cgroupFS = getCgroupFS();
        if (!cgroupFS) return 0;

        auto localHierarchy = getLocalHierarchy(*cgroupFS);
        auto cpuFile = localHierarchy.ourCgroupPath / "cpu.max";

        auto cpuMax = readFile(cpuFile);
        auto cpuMaxParts = tokenizeString<std::vector<std::string>>(cpuMax, " \n");

        if (cpuMaxParts.size() != 2) {
            return 0;
        }

        auto quota = cpuMaxParts[0];
        auto period = cpuMaxParts[1];
        if (quota != "max")
                return std::ceil(std::stoi(quota) / std::stof(period));
    } catch (Error &) { ignoreExceptionInDestructor(lvlDebug); }
    #endif

    return 0;
}

rlim_t savedStackSize = 0;

void setStackSize(rlim_t stackSize)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur < stackSize) {
        savedStackSize = limit.rlim_cur;
        limit.rlim_cur = std::min(stackSize, limit.rlim_max);
        if (setrlimit(RLIMIT_STACK, &limit) != 0) {
            printError(
                "Failed to increase stack size from %1% to %2% (maximum allowed stack size: %3%): "
                "%4%",
                savedStackSize,
                stackSize,
                limit.rlim_max,
                std::strerror(errno)
            );
        }
    }
}

void restoreProcessContext(bool restoreMounts)
{
    restoreSignals();
    if (restoreMounts) {
        restoreMountNamespace();
    }

    if (savedStackSize) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit) == 0) {
            limit.rlim_cur = savedStackSize;
            setrlimit(RLIMIT_STACK, &limit);
        }
    }
}

std::optional<Path> getSelfExe()
{
    static auto cached = []() -> std::optional<Path>
    {
        #if __linux__
        return readLink("/proc/self/exe");
        #elif __APPLE__
        char buf[1024];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return buf;
        else
            return std::nullopt;
        #elif __FreeBSD__
        int sysctlName[] = {
            CTL_KERN,
            KERN_PROC,
            KERN_PROC_PATHNAME,
            -1,
        };
        size_t pathLen = 0;
        if (sysctl(sysctlName, sizeof(sysctlName) / sizeof(sysctlName[0]), nullptr, &pathLen, nullptr, 0) < 0) {
	        return std::nullopt;
        }

        std::vector<char> path(pathLen);
        if (sysctl(sysctlName, sizeof(sysctlName) / sizeof(sysctlName[0]), path.data(), &pathLen, nullptr, 0) < 0) {
            return std::nullopt;
        }

        return Path(path.begin(), path.end());
        #else
        return std::nullopt;
        #endif
    }();
    return cached;
}

}
