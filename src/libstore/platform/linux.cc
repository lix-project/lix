#include "build/worker.hh"
#include "cgroup.hh"
#include "finally.hh"
#include "gc-store.hh"
#include "signals.hh"
#include "platform/linux.hh"
#include "regex.hh"

#include <grp.h>
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

Pid LinuxLocalDerivationGoal::startChild(std::function<void()> openSlave)
{
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
         only has a private loopback interface. (Fixed-output
         derivations are not run in a private network namespace
         to allow functions like fetchurl to work.)

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
        if (privateNetwork)
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

    /* Save the mount- and user namespace of the child. We have to do this
       *before* the child does a chroot. */
    sandboxMountNamespace = AutoCloseFD{open(fmt("/proc/%d/ns/mnt", pid.get()).c_str(), O_RDONLY)};
    if (sandboxMountNamespace.get() == -1)
        throw SysError("getting sandbox mount namespace");

    if (usingUserNamespace) {
        sandboxUserNamespace = AutoCloseFD{open(fmt("/proc/%d/ns/user", pid.get()).c_str(), O_RDONLY)};
        if (sandboxUserNamespace.get() == -1)
            throw SysError("getting sandbox user namespace");
    }

    /* Move the child into its own cgroup. */
    if (cgroup)
        writeFile(*cgroup + "/cgroup.procs", fmt("%d", pid.get()));

    /* Signal the builder that we've updated its user namespace. */
    writeFull(userNamespaceSync.writeSide.get(), "1");

    return pid;
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
