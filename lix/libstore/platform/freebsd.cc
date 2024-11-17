#include "lix/libstore/platform/freebsd.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <libprocstat.h>

namespace nix {

static void readSysctlRoots(const char * name, UncheckedRoots & unchecked)
{
    size_t len = 0;
    std::string value;
    if (int err = sysctlbyname(name, nullptr, &len, nullptr, 0) < 0) {
        if (err == ENOENT || err == EACCES) {
            return;
        } else {
            throw SysError(err, "sysctlbyname %1%", name);
        }
    }

    value.resize(len, ' ');
    if (int err = sysctlbyname(name, value.data(), &len, nullptr, 0) < 0) {
        if (err == ENOENT || err == EACCES) {
            return;
        } else {
            throw SysError(err, "sysctlbyname %1%", name);
        }
    }

    for (auto & path : tokenizeString<Strings>(value, ";")) {
        unchecked[path].emplace(fmt("{{sysctl:%1%}}", name));
    }
}

struct ProcstatDeleter
{
    void operator()(struct procstat * ps)
    {
        procstat_close(ps);
    }
};

template<auto del>
struct ProcstatReferredDeleter
{
    struct procstat * ps;

    ProcstatReferredDeleter(struct procstat * ps) : ps(ps) {}

    template<typename T>
    void operator()(T * p)
    {
        del(ps, p);
    }
};

void FreeBSDLocalStore::findPlatformRoots(UncheckedRoots & unchecked)
{
    readSysctlRoots("kern.module_path", unchecked);

    auto storePathRegex = regex::storePathRegex(config().storeDir);

    auto ps = std::unique_ptr<struct procstat, ProcstatDeleter>(procstat_open_sysctl());
    if (!ps) {
        throw SysError("procstat_open_sysctl");
    }

    auto procs = std::unique_ptr<struct kinfo_proc[], ProcstatReferredDeleter<procstat_freeprocs>>(
        nullptr, ps.get()
    );
    auto files = std::unique_ptr<struct filestat_list, ProcstatReferredDeleter<procstat_freefiles>>(
        nullptr, ps.get()
    );

    unsigned int numprocs = 0;
    procs.reset(procstat_getprocs(ps.get(), KERN_PROC_PROC, 0, &numprocs));
    if (!procs || numprocs == 0) {
        throw SysError("procstat_getprocs");
    };

    for (unsigned int procidx = 0; procidx < numprocs; procidx++) {
        // Includes file descriptors, executable, cwd,
        // and mmapped files (including dynamic libraries)
        files.reset(procstat_getfiles(ps.get(), &procs[procidx], 1));
        // We only have permission if we're root so just skip it if we fail
        if (!files) {
            continue;
        }

        for (struct filestat * file = files->stqh_first; file; file = file->next.stqe_next) {
            if (!file->fs_path) {
                continue;
            }

            std::string role;
            if (file->fs_uflags & PS_FST_UFLAG_CTTY) {
                role = "ctty";
            } else if (file->fs_uflags & PS_FST_UFLAG_CDIR) {
                role = "cwd";
            } else if (file->fs_uflags & PS_FST_UFLAG_JAIL) {
                role = "jail";
            } else if (file->fs_uflags & PS_FST_UFLAG_RDIR) {
                role = "root";
            } else if (file->fs_uflags & PS_FST_UFLAG_TEXT) {
                role = "text";
            } else if (file->fs_uflags & PS_FST_UFLAG_TRACE) {
                role = "trace";
            } else if (file->fs_uflags & PS_FST_UFLAG_MMAP) {
                role = "mmap";
            } else {
                role = fmt("fd/%1%", file->fs_fd);
            }

            unchecked[file->fs_path].emplace(fmt("{procstat:%1%/%2%}", procs[procidx].ki_pid, role)
            );
        }

        auto env_name = fmt("{procstat:%1%/env}", procs[procidx].ki_pid);
        // No need to free, the buffer is reused on next call and deallocated in procstat_close
        char ** env = procstat_getenvv(ps.get(), &procs[procidx], 0);
        if (env == nullptr) {
            continue;
        }

        for (size_t i = 0; env[i]; i++) {
            auto envString = std::string(env[i]);

            auto envEnd = std::sregex_iterator{};
            for (auto match =
                     std::sregex_iterator{envString.begin(), envString.end(), storePathRegex};
                 match != envEnd;
                 match++)
            {
                unchecked[match->str()].emplace(env_name);
            }
        }
    }
}

void registerLocalStore()
{
    StoreImplementations::add<FreeBSDLocalStore, LocalStoreConfig>();
}

}
