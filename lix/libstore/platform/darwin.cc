#include "lix/libstore/gc-store.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/platform/darwin.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"

#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <libproc.h>
#include <spawn.h>

#include <cstddef>
#include <regex>

namespace nix {

kj::Promise<Result<void>> DarwinLocalStore::findPlatformRoots(UncheckedRoots & unchecked)
try {
    auto storePathRegex = regex::storePathRegex(config().storeDir);

    std::vector<int> pids;
    std::size_t pidBufSize = 1;

    while (pidBufSize > pids.size() * sizeof(int)) {
        // Reserve some extra size so we don't fail too much
        pids.resize((pidBufSize + pidBufSize / 8) / sizeof(int));
        auto size = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), pids.size() * sizeof(int));

        if (size <= 0) {
            throw SysError("Listing PIDs");
        }
        pidBufSize = size;
    }

    pids.resize(pidBufSize / sizeof(int));

    for (auto pid : pids) {
        // It doesn't make sense to ask about the kernel
        if (pid == 0) {
            continue;
        }

        try {
            // Process cwd/root directory
            struct proc_vnodepathinfo vnodeInfo;
            if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vnodeInfo, sizeof(vnodeInfo)) <= 0) {
                throw SysError("Getting pid %1% working directory", pid);
            }

            unchecked[std::string(vnodeInfo.pvi_cdir.vip_path)].emplace(fmt("{libproc/%d/cwd}", pid)
            );
            unchecked[std::string(vnodeInfo.pvi_rdir.vip_path)].emplace(
                fmt("{libproc/%d/rootdir}", pid)
            );

            // File descriptors
            std::vector<struct proc_fdinfo> fds;
            std::size_t fdBufSize = 1;
            while (fdBufSize > fds.size() * sizeof(struct proc_fdinfo)) {
                // Reserve some extra size so we don't fail too much
                fds.resize((fdBufSize + fdBufSize / 8) / sizeof(struct proc_fdinfo));
                errno = 0;
                auto size = proc_pidinfo(
                    pid, PROC_PIDLISTFDS, 0, fds.data(), fds.size() * sizeof(struct proc_fdinfo)
                );

                // errno == 0???! Yes, seriously. This is because macOS has a
                // broken syscall wrapper for proc_pidinfo that has no way of
                // dealing with the system call successfully returning 0. It
                // takes the -1 error result from the errno-setting syscall
                // wrapper and turns it into a 0 result. But what if the system
                // call actually returns 0? Then you get an errno of success.
                //
                // https://github.com/apple-opensource/xnu/blob/4f43d4276fc6a87f2461a3ab18287e4a2e5a1cc0/libsyscall/wrappers/libproc/libproc.c#L100-L110
                // https://git.lix.systems/lix-project/lix/issues/446#issuecomment-5483
                // FB14695751
                if (size <= 0) {
                    if (errno == 0) {
                        fdBufSize = 0;
                        break;
                    } else {
                        throw SysError("Listing pid %1% file descriptors", pid);
                    }
                }
                fdBufSize = size;
            }
            fds.resize(fdBufSize / sizeof(struct proc_fdinfo));

            for (auto fd : fds) {
                // By definition, only a vnode is on the filesystem
                if (fd.proc_fdtype != PROX_FDTYPE_VNODE) {
                    continue;
                }

                struct vnode_fdinfowithpath fdInfo;
                if (proc_pidfdinfo(
                        pid, fd.proc_fd, PROC_PIDFDVNODEPATHINFO, &fdInfo, sizeof(fdInfo)
                    )
                    <= 0)
                {
                    // They probably just closed this fd, no need to cancel looking at ranges and
                    // arguments
                    if (errno == EBADF) {
                        continue;
                    }
                    throw SysError("Getting pid %1% fd %2% path", pid, fd.proc_fd);
                }

                unchecked[std::string(fdInfo.pvip.vip_path)].emplace(
                    fmt("{libproc/%d/fd/%d}", pid, fd.proc_fd)
                );
            }

            // Regions (e.g. mmapped files, executables, shared libraries)
            uint64_t nextAddr = 0;
            while (true) {
                // Seriously, what are you doing XNU?
                // There's 3 flavors of PROC_PIDREGIONPATHINFO:
                // * PROC_PIDREGIONPATHINFO includes all regions
                // * PROC_PIDREGIONPATHINFO2 includes regions backed by a vnode
                // * PROC_PIDREGIONPATHINFO3 includes regions backed by a vnode on a specified
                // filesystem Only PROC_PIDREGIONPATHINFO is documented. Unfortunately, using it
                // would make finding gcroots take about 100x as long and tests would fail from
                // timeout. According to the Frida source code, PROC_PIDREGIONPATHINFO2 has been
                // available since XNU 2782.1.97 in OS X 10.10
                //
                // 22 means PROC_PIDREGIONPATHINFO2
                struct proc_regionwithpathinfo regionInfo;
                if (proc_pidinfo(pid, 22, nextAddr, &regionInfo, sizeof(regionInfo)) <= 0) {
                    // PROC_PIDREGIONPATHINFO signals we're done with an error,
                    // so we're expected to hit this once per process
                    if (errno == ESRCH || errno == EINVAL) {
                        break;
                    }
                    throw SysError("Getting pid %1% region path", pid);
                }

                unchecked[std::string(regionInfo.prp_vip.vip_path)].emplace(
                    fmt("{libproc/%d/region}", pid)
                );

                nextAddr = regionInfo.prp_prinfo.pri_address + regionInfo.prp_prinfo.pri_size;
            }

            // Arguments and environment variables
            // We can't read environment variables of binaries with entitlements unless
            // nix has the `com.apple.private.read-environment-variables` entitlement or SIP is off
            // We can read arguments for all applications though.

            // Yes, it's a sysctl, the proc_info and sysctl APIs are mostly similar,
            // but both have exclusive capabilities
            int sysctlName[3] = {CTL_KERN, KERN_PROCARGS2, pid};
            size_t argsSize = 0;
            if (sysctl(sysctlName, 3, nullptr, &argsSize, nullptr, 0) < 0) {
                throw SysError("Reading pid %1% arguments", pid);
            }

            std::vector<char> args(argsSize);
            if (sysctl(sysctlName, 3, args.data(), &argsSize, nullptr, 0) < 0) {
                throw SysError("Reading pid %1% arguments", pid);
            }

            if (argsSize < args.size()) {
                args.resize(argsSize);
            }

            // We have these perfectly nice arguments, but have to ignore them because
            // otherwise we'd see arguments to nix-store commands and
            // `nix-store --delete /nix/store/whatever` would always fail
            // First 4 bytes are an int of argc.
            if (args.size() < sizeof(int)) {
                continue;
            }
            auto argc = reinterpret_cast<int *>(args.data())[0];

            auto argsIter = args.begin();
            std::advance(argsIter, sizeof(int));
            // Executable then argc args, each separated by some number of null bytes
            for (int i = 0; argsIter != args.end() && i < argc + 1; i++) {
                argsIter = std::find(argsIter, args.end(), '\0');
                argsIter = std::find_if(argsIter, args.end(), [](char ch) { return ch != '\0'; });
            }

            if (argsIter != args.end()) {
                auto env_end = std::sregex_iterator{};
                for (auto i = std::sregex_iterator{argsIter, args.end(), storePathRegex};
                     i != env_end;
                     ++i)
                {
                    unchecked[i->str()].emplace(fmt("{libproc/%d/environ}", pid));
                }
            };

            // Per-thread working directories
            struct proc_taskallinfo taskAllInfo;
            if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &taskAllInfo, sizeof(taskAllInfo)) <= 0) {
                throw SysError("Reading pid %1% tasks", pid);
            }

            // If the process doesn't have the per-thread cwd flag then we already have the
            // process-wide cwd from PROC_PIDVNODEPATHINFO
            if (taskAllInfo.pbsd.pbi_flags & PROC_FLAG_THCWD) {
                std::vector<uint64_t> tids(taskAllInfo.ptinfo.pti_threadnum);
                int tidBufSize = proc_pidinfo(
                    pid, PROC_PIDLISTTHREADS, 0, tids.data(), tids.size() * sizeof(uint64_t)
                );
                if (tidBufSize <= 0) {
                    throw SysError("Listing pid %1% threads", pid);
                }

                for (auto tid : tids) {
                    struct proc_threadwithpathinfo threadPathInfo;
                    if (proc_pidinfo(
                            pid,
                            PROC_PIDTHREADPATHINFO,
                            tid,
                            &threadPathInfo,
                            sizeof(threadPathInfo)
                        )
                        <= 0)
                    {
                        throw SysError("Reading pid %1% thread %2% cwd", pid, tid);
                    }

                    unchecked[std::string(threadPathInfo.pvip.vip_path)].emplace(
                        fmt("{libproc/%d/thread/%d/cwd}", pid, tid)
                    );
                }
            }
        } catch (SysError & e) {
            // ENOENT/ESRCH: Process no longer exists (proc_info)
            // EINVAL: Process no longer exists (sysctl)
            // EACCESS/EPERM: We don't have permission to read this field (proc_info)
            // EIO: Kernel failed to read from target process memory during KERN_PROCARGS2 (sysctl)
            if (errno == ENOENT || errno == ESRCH || errno == EINVAL || errno == EACCES
                || errno == EPERM || errno == EIO)
            {
                continue;
            }
            throw;
        }
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

void DarwinLocalDerivationGoal::execBuilder(std::string builder, Strings args, Strings envStrs)
{
    posix_spawnattr_t attrp;

    if (posix_spawnattr_init(&attrp))
        throw SysError("failed to initialize builder");

    if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
        throw SysError("failed to initialize builder");

    if (drv->platform == "aarch64-darwin") {
        // Unset kern.curproc_arch_affinity so we can escape Rosetta
        int affinity = 0;
        sysctlbyname("kern.curproc_arch_affinity", nullptr, nullptr, &affinity, sizeof(affinity));

        cpu_type_t cpu = CPU_TYPE_ARM64;
        posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, nullptr);
    } else if (drv->platform == "x86_64-darwin") {
        cpu_type_t cpu = CPU_TYPE_X86_64;
        posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, nullptr);
    }

    posix_spawn(nullptr, builder.c_str(), nullptr, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
}

void registerLocalStore() {
    StoreImplementations::add<DarwinLocalStore, LocalStoreConfig>();
}

}
