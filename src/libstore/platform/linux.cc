#include "build/worker.hh"
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
    if (parsedDrv->useUidRange())
        chownToBuilder(chrootRootDir + "/etc");

    if (parsedDrv->useUidRange() && (!buildUser || buildUser->getUIDCount() < 65536))
        throw Error("feature 'uid-range' requires the setting '%s' to be enabled", settings.autoAllocateUids.name);

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
    Path chrootStoreDir = chrootRootDir + worker.store.storeDir;
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
    for (auto & i : drv->outputsAndOptPaths(worker.store)) {
        /* If the name isn't known a priori (i.e. floating
           content-addressed derivation), the temporary location we use
           should be fresh.  Freshness means it is impossible that the path
           is already in the sandbox, so we don't need to worry about
           removing it.  */
        if (i.second.second)
            pathsInChroot.erase(worker.store.printStorePath(*i.second.second));
    }

    if (cgroup) {
        if (mkdir(cgroup->c_str(), 0755) != 0)
            throw SysError("creating cgroup '%s'", *cgroup);
        chownToBuilder(*cgroup);
        chownToBuilder(*cgroup + "/cgroup.procs");
        chownToBuilder(*cgroup + "/cgroup.threads");
        //chownToBuilder(*cgroup + "/cgroup.subtree_control");
    }
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
