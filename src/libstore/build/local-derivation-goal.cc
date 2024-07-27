#include "local-derivation-goal.hh"
#include "indirect-root-store.hh"
#include "hook-instance.hh"
#include "store-api.hh"
#include "worker.hh"
#include "builtins.hh"
#include "builtins/buildenv.hh"
#include "path-references.hh"
#include "finally.hh"
#include "archive.hh"
#include "compression.hh"
#include "daemon.hh"
#include "topo-sort.hh"
#include "json-utils.hh"
#include "cgroup.hh"
#include "personality.hh"
#include "namespaces.hh"
#include "child.hh"
#include "unix-domain-socket.hh"
#include "mount.hh"

#include <regex>
#include <queue>

#include <sys/un.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

/* Includes required for chroot support. */
#if __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#if __APPLE__
/* This definition is undocumented but depended upon by all major browsers. */
extern "C" int sandbox_init_with_parameters(const char *profile, uint64_t flags, const char *const parameters[], char **errorbuf);
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>

namespace nix {

namespace {
/**
 * The system for which Nix is compiled.
 */
[[gnu::unused]]
constexpr const std::string_view nativeSystem = SYSTEM;
}

void handleDiffHook(
    uid_t uid, uid_t gid,
    const Path & tryA, const Path & tryB,
    const Path & drvPath, const Path & tmpDir)
{
    auto & diffHookOpt = settings.diffHook.get();
    if (diffHookOpt && settings.runDiffHook) {
        auto & diffHook = *diffHookOpt;
        try {
            auto diffRes = runProgram(RunOptions {
                .program = diffHook,
                .searchPath = true,
                .args = {tryA, tryB, drvPath, tmpDir},
                .uid = uid,
                .gid = gid,
                .chdir = "/"
            });
            if (!statusOk(diffRes.first))
                throw ExecError(diffRes.first,
                    "diff-hook program '%1%' %2%",
                    diffHook,
                    statusToString(diffRes.first));

            if (diffRes.second != "")
                printError(chomp(diffRes.second));
        } catch (Error & error) {
            ErrorInfo ei = error.info();
            // FIXME: wrap errors.
            ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
            logError(ei);
        }
    }
}

const Path LocalDerivationGoal::homeDir = "/homeless-shelter";


LocalDerivationGoal::~LocalDerivationGoal() noexcept(false)
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { deleteTmpDir(false); } catch (...) { ignoreException(); }
    try { killChild(); } catch (...) { ignoreException(); }
    try { stopDaemon(); } catch (...) { ignoreException(); }
}


bool LocalDerivationGoal::needsHashRewrite()
{
    return !useChroot;
}


LocalStore & LocalDerivationGoal::getLocalStore()
{
    auto p = dynamic_cast<LocalStore *>(&worker.store);
    assert(p);
    return *p;
}


void LocalDerivationGoal::killChild()
{
    if (pid) {
        worker.childTerminated(this);

        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        ::kill(-pid.get(), SIGKILL); /* ignore the result */

        killSandbox(true);

        pid.wait();
    }

    DerivationGoal::killChild();
}


void LocalDerivationGoal::killSandbox(bool getStats)
{
    if (buildUser) {
        auto uid = buildUser->getUID();
        assert(uid != 0);
        killUser(uid);
    }
}


void LocalDerivationGoal::tryLocalBuild()
{
#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    unsigned int curBuilds = worker.getNrLocalBuilds();
    if (curBuilds >= settings.maxBuildJobs) {
        state = &DerivationGoal::tryToBuild;
        worker.waitForBuildSlot(shared_from_this());
        outputLocks.unlock();
        return;
    }

    assert(derivationType);

    /* Are we doing a chroot build? */
    {
        auto noChroot = parsedDrv->getBoolAttr("__noChroot");
        if (settings.sandboxMode == smEnabled) {
            if (noChroot)
                throw Error("derivation '%s' has '__noChroot' set, "
                    "but that's not allowed when 'sandbox' is 'true'", worker.store.printStorePath(drvPath));
#if __APPLE__
            if (additionalSandboxProfile != "")
                throw Error("derivation '%s' specifies a sandbox profile, "
                    "but this is only allowed when 'sandbox' is 'relaxed'", worker.store.printStorePath(drvPath));
#endif
            useChroot = true;
        }
        else if (settings.sandboxMode == smDisabled)
            useChroot = false;
        else if (settings.sandboxMode == smRelaxed)
            useChroot = derivationType->isSandboxed() && !noChroot;
    }

    auto & localStore = getLocalStore();
    if (localStore.storeDir != localStore.realStoreDir.get()) {
        #if __linux__
            useChroot = true;
        #else
            throw Error("building using a diverted store is not supported on this platform");
        #endif
    }

    if (useBuildUsers()) {
        if (!buildUser)
            buildUser = acquireUserLock(parsedDrv->useUidRange() ? 65536 : 1, useChroot);

        if (!buildUser) {
            if (!actLock)
                actLock = std::make_unique<Activity>(*logger, lvlWarn, actBuildWaiting,
                    fmt("waiting for a free build user ID for '%s'", Magenta(worker.store.printStorePath(drvPath))));
            worker.waitForAWhile(shared_from_this());
            return;
        }
    }

    #if __linux__
    if (useChroot) {
        // FIXME: should user namespaces being unsupported also require
        // sandbox-fallback to be allowed? I don't think so, since they aren't a
        // huge security win to have enabled.
        usingUserNamespace = userNamespacesSupported();

        if (!mountAndPidNamespacesSupported()) {
            if (!settings.sandboxFallback)
                throw Error("this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing. Pass --debug for diagnostics on what is broken.");
            debug("auto-disabling sandboxing because the prerequisite namespaces are not available");
            useChroot = false;
        }

        if (!usingUserNamespace && !buildUser) {
            throw Error("cannot perform a sandboxed build because user namespaces are not available.\nIn this Lix's configuration, user namespaces are required due to either being non-root, or build-users-group being disabled without also enabling auto-allocate-uids");
        }
    }
    #endif

    actLock.reset();

    try {

        /* Okay, we have to build. */
        startBuilder();

    } catch (BuildError & e) {
        outputLocks.unlock();
        buildUser.reset();
        worker.permanentFailure = true;
        done(BuildResult::InputRejected, {}, std::move(e));
        return;
    }

    /* This state will be reached when we get EOF on the child's
       log pipe. */
    state = &DerivationGoal::buildDone;

    started();
}


/* Move/rename path 'src' to 'dst'. Temporarily make 'src' writable if
   it's a directory and we're not root (to be able to update the
   directory's parent link ".."). */
static void movePath(const Path & src, const Path & dst)
{
    auto st = lstat(src);

    bool changePerm = (geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));

    if (changePerm)
        chmodPath(src, st.st_mode | S_IWUSR);

    renameFile(src, dst);

    if (changePerm)
        chmodPath(dst, st.st_mode);
}


extern void replaceValidPath(const Path & storePath, const Path & tmpPath);


int LocalDerivationGoal::getChildStatus()
{
    return hook ? DerivationGoal::getChildStatus() : pid.kill();
}

void LocalDerivationGoal::closeReadPipes()
{
    if (hook) {
        DerivationGoal::closeReadPipes();
    } else
        builderOut.close();
}


void LocalDerivationGoal::cleanupHookFinally()
{
    /* Release the build user at the end of this function. We don't do
       it right away because we don't want another build grabbing this
       uid and then messing around with our output. */
    buildUser.reset();
}


void LocalDerivationGoal::cleanupPreChildKill()
{
    sandboxMountNamespace.reset();
    sandboxUserNamespace.reset();
}


void LocalDerivationGoal::cleanupPostChildKill()
{
    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    killSandbox(true);

    /* Terminate the recursive Nix daemon. */
    stopDaemon();
}


bool LocalDerivationGoal::cleanupDecideWhetherDiskFull()
{
    bool diskFull = false;

    /* Heuristically check whether the build failure may have
       been caused by a disk full condition.  We have no way
       of knowing whether the build actually got an ENOSPC.
       So instead, check if the disk is (nearly) full now.  If
       so, we don't mark this build as a permanent failure. */
#if HAVE_STATVFS
    {
        auto & localStore = getLocalStore();
        uint64_t required = 8ULL * 1024 * 1024; // FIXME: make configurable
        struct statvfs st;
        if (statvfs(localStore.realStoreDir.get().c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
        if (statvfs(tmpDir.c_str(), &st) == 0 &&
            (uint64_t) st.f_bavail * st.f_bsize < required)
            diskFull = true;
    }
#endif

    deleteTmpDir(false);

    /* Move paths out of the chroot for easier debugging of
       build failures. */
    if (useChroot && buildMode == bmNormal)
        for (auto & [_, status] : initialOutputs) {
            if (!status.known) continue;
            if (buildMode != bmCheck && status.known->isValid()) continue;
            auto p = worker.store.toRealPath(status.known->path);
            if (pathExists(chrootRootDir + p))
                renameFile((chrootRootDir + p), p);
        }

    return diskFull;
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeCheck()
{
    deleteTmpDir(true);
}


void LocalDerivationGoal::cleanupPostOutputsRegisteredModeNonCheck()
{
    /* Delete unused redirected outputs (when doing hash rewriting). */
    for (auto & i : redirectedOutputs)
        deletePath(worker.store.Store::toRealPath(i.second));

    /* Delete the chroot (if we were using one). */
    autoDelChroot.reset(); /* this runs the destructor */

    cleanupPostOutputsRegisteredModeCheck();
}

void LocalDerivationGoal::startBuilder()
{
    if ((buildUser && buildUser->getUIDCount() != 1)
        #if __linux__
        || settings.useCgroups
        #endif
        )
    {
        #if __linux__
        experimentalFeatureSettings.require(Xp::Cgroups);

        auto cgroupFS = getCgroupFS();
        if (!cgroupFS)
            throw Error("cannot determine the cgroups file system");

        auto ourCgroups = getCgroups("/proc/self/cgroup");
        auto ourCgroup = ourCgroups[""];
        if (ourCgroup == "")
            throw Error("cannot determine cgroup name from /proc/self/cgroup");

        auto ourCgroupPath = canonPath(*cgroupFS + "/" + ourCgroup);

        if (!pathExists(ourCgroupPath))
            throw Error("expected cgroup directory '%s'", ourCgroupPath);

        static std::atomic<unsigned int> counter{0};

        cgroup = buildUser
            ? fmt("%s/nix-build-uid-%d", ourCgroupPath, buildUser->getUID())
            : fmt("%s/nix-build-pid-%d-%d", ourCgroupPath, getpid(), counter++);

        debug("using cgroup '%s'", *cgroup);

        /* When using a build user, record the cgroup we used for that
           user so that if we got interrupted previously, we can kill
           any left-over cgroup first. */
        if (buildUser) {
            auto cgroupsDir = settings.nixStateDir + "/cgroups";
            createDirs(cgroupsDir);

            auto cgroupFile = fmt("%s/%d", cgroupsDir, buildUser->getUID());

            if (pathExists(cgroupFile)) {
                auto prevCgroup = readFile(cgroupFile);
                destroyCgroup(prevCgroup);
            }

            writeFile(cgroupFile, *cgroup);
        }

        #else
        throw Error("cgroups are not supported on this platform");
        #endif
    }

    /* Make sure that no other processes are executing under the
       sandbox uids. This must be done before any chownToBuilder()
       calls. */
    killSandbox(false);

    /* Right platform? */
    if (!parsedDrv->canBuildLocally(worker.store))
        throw Error("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}",
            drv->platform,
            concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
            worker.store.printStorePath(drvPath),
            settings.thisSystem,
            concatStringsSep<StringSet>(", ", worker.store.systemFeatures));

    /* Create a temporary directory where the build will take
       place. */
    tmpDir = createTempDir(settings.buildDir.get().value_or(""), "nix-build-" + std::string(drvPath.name()), false, false, 0700);

    chownToBuilder(tmpDir);

    for (auto & [outputName, status] : initialOutputs) {
        /* Set scratch path we'll actually use during the build.

           If we're not doing a chroot build, but we have some valid
           output paths.  Since we can't just overwrite or delete
           them, we have to do hash rewriting: i.e. in the
           environment/arguments passed to the build, we replace the
           hashes of the valid outputs with unique dummy strings;
           after the build, we discard the redirected outputs
           corresponding to the valid outputs, and rewrite the
           contents of the new outputs to replace the dummy strings
           with the actual hashes. */
        auto scratchPath =
            !status.known
                ? makeFallbackPath(outputName)
            : !needsHashRewrite()
                /* Can always use original path in sandbox */
                ? status.known->path
            : !status.known->isPresent()
                /* If path doesn't yet exist can just use it */
                ? status.known->path
            : buildMode != bmRepair && !status.known->isValid()
                /* If we aren't repairing we'll delete a corrupted path, so we
                   can use original path */
                ? status.known->path
            :   /* If we are repairing or the path is totally valid, we'll need
                   to use a temporary path */
                makeFallbackPath(status.known->path);
        scratchOutputs.insert_or_assign(outputName, scratchPath);

        /* Substitute output placeholders with the scratch output paths.
           We'll use during the build. */
        inputRewrites[hashPlaceholder(outputName)] = worker.store.printStorePath(scratchPath);

        /* Additional tasks if we know the final path a priori. */
        if (!status.known) continue;
        auto fixedFinalPath = status.known->path;

        /* Additional tasks if the final and scratch are both known and
           differ. */
        if (fixedFinalPath == scratchPath) continue;

        /* Ensure scratch path is ours to use. */
        deletePath(worker.store.printStorePath(scratchPath));

        /* Rewrite and unrewrite paths */
        {
            std::string h1 { fixedFinalPath.hashPart() };
            std::string h2 { scratchPath.hashPart() };
            inputRewrites[h1] = h2;
        }

        redirectedOutputs.insert_or_assign(std::move(fixedFinalPath), std::move(scratchPath));
    }

    /* Construct the environment passed to the builder. */
    initEnv();

    writeStructuredAttrs();

    /* Handle exportReferencesGraph(), if set. */
    if (!parsedDrv->getStructuredAttrs()) {
        /* The `exportReferencesGraph' feature allows the references graph
           to be passed to a builder.  This attribute should be a list of
           pairs [name1 path1 name2 path2 ...].  The references graph of
           each `pathN' will be stored in a text file `nameN' in the
           temporary build directory.  The text files have the format used
           by `nix-store --register-validity'.  However, the deriver
           fields are left empty. */
        auto s = getOr(drv->env, "exportReferencesGraph", "");
        Strings ss = tokenizeString<Strings>(s);
        if (ss.size() % 2 != 0)
            throw BuildError("odd number of tokens in 'exportReferencesGraph': '%1%'", s);
        for (Strings::iterator i = ss.begin(); i != ss.end(); ) {
            auto fileName = *i++;
            static std::regex regex("[A-Za-z_][A-Za-z0-9_.-]*");
            if (!std::regex_match(fileName, regex))
                throw Error("invalid file name '%s' in 'exportReferencesGraph'", fileName);

            auto storePathS = *i++;
            if (!worker.store.isInStore(storePathS))
                throw BuildError("'exportReferencesGraph' contains a non-store path '%1%'", storePathS);
            auto storePath = worker.store.toStorePath(storePathS).first;

            /* Write closure info to <fileName>. */
            writeFile(tmpDir + "/" + fileName,
                worker.store.makeValidityRegistration(
                    worker.store.exportReferences({storePath}, inputPaths), false, false));
        }
    }

    if (useChroot) {

        /* Allow a user-configurable set of directories from the
           host file system. */
        pathsInChroot.clear();

        for (auto i : settings.sandboxPaths.get()) {
            if (i.empty()) continue;
            bool optional = false;
            if (i[i.size() - 1] == '?') {
                optional = true;
                i.pop_back();
            }
            size_t p = i.find('=');
            if (p == std::string::npos)
                pathsInChroot[i] = {i, optional};
            else
                pathsInChroot[i.substr(0, p)] = {i.substr(p + 1), optional};
        }
        if (worker.store.storeDir.starts_with(tmpDirInSandbox))
        {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        StorePathSet closure;
        for (auto & i : pathsInChroot)
            try {
                if (worker.store.isInStore(i.second.source))
                    worker.store.computeFSClosure(worker.store.toStorePath(i.second.source).first, closure);
            } catch (InvalidPath & e) {
            } catch (Error & e) {
                e.addTrace({}, "while processing 'sandbox-paths'");
                throw;
            }
        for (auto & i : closure) {
            auto p = worker.store.printStorePath(i);
            pathsInChroot.insert_or_assign(p, p);
        }

        PathSet allowedPaths = settings.allowedImpureHostPrefixes;

        /* This works like the above, except on a per-derivation level */
        auto impurePaths = parsedDrv->getStringsAttr("__impureHostDeps").value_or(Strings());

        for (auto & i : impurePaths) {
            bool found = false;
            /* Note: we're not resolving symlinks here to prevent
               giving a non-root user info about inaccessible
               files. */
            Path canonI = canonPath(i);
            /* If only we had a trie to do this more efficiently :) luckily, these are generally going to be pretty small */
            for (auto & a : allowedPaths) {
                Path canonA = canonPath(a);
                if (canonI == canonA || isInDir(canonI, canonA)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                throw Error("derivation '%s' requested impure path '%s', but it was not in allowed-impure-host-deps",
                    worker.store.printStorePath(drvPath), i);

            /* Allow files in __impureHostDeps to be missing; e.g.
               macOS 11+ has no /usr/lib/libSystem*.dylib */
            pathsInChroot[i] = {i, true};
        }

        if (parsedDrv->useUidRange() && !supportsUidRange())
            throw Error("feature 'uid-range' is not supported on this platform");

        prepareSandbox();

    } else {
        if (parsedDrv->useUidRange())
            throw Error("feature 'uid-range' is only supported in sandboxed builds");
    }

    if (needsHashRewrite() && pathExists(homeDir))
        throw Error("home directory '%1%' exists; please remove it to assure purity of builds without sandboxing", homeDir);

    if (useChroot && settings.preBuildHook != "" && dynamic_cast<Derivation *>(drv.get())) {
        printMsg(lvlChatty, "executing pre-build hook '%1%'", settings.preBuildHook);
        auto args = useChroot ? Strings({worker.store.printStorePath(drvPath), chrootRootDir}) :
            Strings({ worker.store.printStorePath(drvPath) });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        auto lines = runProgram(settings.preBuildHook, false, args);
        auto lastPos = std::string::size_type{0};
        for (auto nlPos = lines.find('\n'); nlPos != std::string::npos;
                nlPos = lines.find('\n', lastPos))
        {
            auto line = lines.substr(lastPos, nlPos - lastPos);
            lastPos = nlPos + 1;
            if (state == stBegin) {
                if (line == "extra-sandbox-paths" || line == "extra-chroot-dirs") {
                    state = stExtraChrootDirs;
                } else {
                    throw Error("unknown pre-build hook command '%1%'", line);
                }
            } else if (state == stExtraChrootDirs) {
                if (line == "") {
                    state = stBegin;
                } else {
                    auto p = line.find('=');
                    if (p == std::string::npos)
                        pathsInChroot[line] = line;
                    else
                        pathsInChroot[line.substr(0, p)] = line.substr(p + 1);
                }
            }
        }
    }

    /* Fire up a Nix daemon to process recursive Nix calls from the
       builder. */
    if (parsedDrv->getRequiredSystemFeatures().count("recursive-nix"))
        startDaemon();

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv->builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv->args));
    for (auto & i : drv->env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

    /* Create the log file. */
    Path logFile = openLogFile();

    /* Create a pseudoterminal to get the output of the builder. */
    builderOut = AutoCloseFD{posix_openpt(O_RDWR | O_NOCTTY)};
    if (!builderOut)
        throw SysError("opening pseudoterminal master");

    // FIXME: not thread-safe, use ptsname_r
    std::string slaveName = ptsname(builderOut.get());

    if (buildUser) {
        if (chmod(slaveName.c_str(), 0600))
            throw SysError("changing mode of pseudoterminal slave");

        if (chown(slaveName.c_str(), buildUser->getUID(), 0))
            throw SysError("changing owner of pseudoterminal slave");
    }
#if __APPLE__
    else {
        if (grantpt(builderOut.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    if (unlockpt(builderOut.get()))
        throw SysError("unlocking pseudoterminal");

    /* Open the slave side of the pseudoterminal and use it as stderr. */
    auto openSlave = [&]()
    {
        AutoCloseFD builderOut{open(slaveName.c_str(), O_RDWR | O_NOCTTY)};
        if (!builderOut)
            throw SysError("opening pseudoterminal slave");

        // Put the pt into raw mode to prevent \n -> \r\n translation.
        struct termios term;
        if (tcgetattr(builderOut.get(), &term))
            throw SysError("getting pseudoterminal attributes");

        cfmakeraw(&term);

        if (tcsetattr(builderOut.get(), TCSANOW, &term))
            throw SysError("putting pseudoterminal into raw mode");

        if (dup2(builderOut.get(), STDERR_FILENO) == -1)
            throw SysError("cannot pipe standard error into log file");
    };

    buildResult.startTime = time(0);

    /* Fork a child to build the package. */
    pid = startChild(openSlave);

    /* parent */
    pid.setSeparatePG(true);
    worker.childStarted(shared_from_this(), {builderOut.get()}, true, true);

    /* Check if setting up the build environment failed. */
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOut.get());
            } catch (Error & e) {
                auto status = pid.wait();
                e.addTrace({}, "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    worker.store.printStorePath(drvPath),
                    statusToString(status),
                    concatStringsSep("|", msgs));
                throw;
            }
        }();
        if (msg.substr(0, 1) == "\2") break;
        if (msg.substr(0, 1) == "\1") {
            FdSource source(builderOut.get());
            auto ex = readError(source);
            ex.addTrace({}, "while setting up the build environment");
            throw ex;
        }
        debug("sandbox setup: " + msg);
        msgs.push_back(std::move(msg));
    }
}


Pid LocalDerivationGoal::startChild(std::function<void()> openSlave) {
    return startProcess([&]() {
        openSlave();
        runChild();
    });
}


void LocalDerivationGoal::initTmpDir() {
    /* In a sandbox, for determinism, always use the same temporary
       directory. */
#if __linux__
    tmpDirInSandbox = useChroot ? settings.sandboxBuildDir : tmpDir;
#else
    tmpDirInSandbox = tmpDir;
#endif

    /* In non-structured mode, add all bindings specified in the
       derivation via the environment, except those listed in the
       passAsFile attribute. Those are passed as file names pointing
       to temporary files containing the contents. Note that
       passAsFile is ignored in structure mode because it's not
       needed (attributes are not passed through the environment, so
       there is no size constraint). */
    if (!parsedDrv->getStructuredAttrs()) {

        StringSet passAsFile = tokenizeString<StringSet>(getOr(drv->env, "passAsFile", ""));
        for (auto & i : drv->env) {
            if (passAsFile.find(i.first) == passAsFile.end()) {
                env[i.first] = i.second;
            } else {
                auto hash = hashString(htSHA256, i.first);
                std::string fn = ".attr-" + hash.to_string(Base32, false);
                Path p = tmpDir + "/" + fn;
                writeFile(p, rewriteStrings(i.second, inputRewrites));
                chownToBuilder(p);
                env[i.first + "Path"] = tmpDirInSandbox + "/" + fn;
            }
        }

    }

    /* For convenience, set an environment pointing to the top build
       directory. */
    env["NIX_BUILD_TOP"] = tmpDirInSandbox;

    /* Also set TMPDIR and variants to point to this directory. */
    env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmpDirInSandbox;

    /* Explicitly set PWD to prevent problems with chroot builds.  In
       particular, dietlibc cannot figure out the cwd because the
       inode of the current directory doesn't appear in .. (because
       getdents returns the inode of the mount point). */
    env["PWD"] = tmpDirInSandbox;
}


void LocalDerivationGoal::initEnv()
{
    env.clear();

    /* Most shells initialise PATH to some default (/bin:/usr/bin:...) when
       PATH is not set.  We don't want this, so we fill it in with some dummy
       value. */
    env["PATH"] = "/path-not-set";

    /* Set HOME to a non-existing path to prevent certain programs from using
       /etc/passwd (or NIS, or whatever) to locate the home directory (for
       example, wget looks for ~/.wgetrc).  I.e., these tools use /etc/passwd
       if HOME is not set, but they will just assume that the settings file
       they are looking for does not exist if HOME is set but points to some
       non-existing path. */
    env["HOME"] = homeDir;

    /* Tell the builder where the Nix store is.  Usually they
       shouldn't care, but this is useful for purity checking (e.g.,
       the compiler or linker might only want to accept paths to files
       in the store or in the build directory). */
    env["NIX_STORE"] = worker.store.storeDir;

    /* The maximum number of cores to utilize for parallel building. */
    env["NIX_BUILD_CORES"] = fmt("%d", settings.buildCores);

    initTmpDir();

    /* Compatibility hack with Nix <= 0.7: if this is a fixed-output
       derivation, tell the builder, so that for instance `fetchurl'
       can skip checking the output.  On older Nixes, this environment
       variable won't be set, so `fetchurl' will do the check. */
    if (derivationType->isFixed()) env["NIX_OUTPUT_CHECKED"] = "1";

    /* *Only* if this is a fixed-output derivation, propagate the
       values of the environment variables specified in the
       `impureEnvVars' attribute to the builder.  This allows for
       instance environment variables for proxy configuration such as
       `http_proxy' to be easily passed to downloaders like
       `fetchurl'.  Passing such environment variables from the caller
       to the builder is generally impure, but the output of
       fixed-output derivations is by definition pure (since we
       already know the cryptographic hash of the output). */
    if (!derivationType->isSandboxed()) {
        for (auto & i : parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings()))
            env[i] = getEnv(i).value_or("");
    }

    /* Currently structured log messages piggyback on stderr, but we
       may change that in the future. So tell the builder which file
       descriptor to use for that. */
    env["NIX_LOG_FD"] = "2";

    /* Trigger colored output in various tools. */
    env["TERM"] = "xterm-256color";
}


void LocalDerivationGoal::writeStructuredAttrs()
{
    if (auto structAttrsJson = parsedDrv->prepareStructuredAttrs(worker.store, inputPaths)) {
        auto json = structAttrsJson.value();
        nlohmann::json rewritten;
        for (auto & [i, v] : json["outputs"].get<nlohmann::json::object_t>()) {
            /* The placeholder must have a rewrite, so we use it to cover both the
               cases where we know or don't know the output path ahead of time. */
            rewritten[i] = rewriteStrings((std::string) v, inputRewrites);
        }

        json["outputs"] = rewritten;

        auto jsonSh = writeStructuredAttrsShell(json);

        writeFile(tmpDir + "/.attrs.sh", rewriteStrings(jsonSh, inputRewrites));
        chownToBuilder(tmpDir + "/.attrs.sh");
        env["NIX_ATTRS_SH_FILE"] = tmpDirInSandbox + "/.attrs.sh";
        writeFile(tmpDir + "/.attrs.json", rewriteStrings(json.dump(), inputRewrites));
        chownToBuilder(tmpDir + "/.attrs.json");
        env["NIX_ATTRS_JSON_FILE"] = tmpDirInSandbox + "/.attrs.json";
    }
}


static StorePath pathPartOfReq(const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const SingleDerivedPath::Built & bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


static StorePath pathPartOfReq(const DerivedPath & req)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const DerivedPath::Built & bfd)  {
            return pathPartOfReq(*bfd.drvPath);
        },
    }, req.raw());
}


bool LocalDerivationGoal::isAllowed(const DerivedPath & req)
{
    return this->isAllowed(pathPartOfReq(req));
}


struct RestrictedStoreConfig : virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;
    const std::string name() override { return "Restricted Store"; }
};

/* A wrapper around LocalStore that only allows building/querying of
   paths that are in the input closures of the build or were added via
   recursive Nix calls. */
struct RestrictedStore : public virtual RestrictedStoreConfig, public virtual IndirectRootStore, public virtual GcStore
{
    ref<LocalStore> next;

    LocalDerivationGoal & goal;

    RestrictedStore(const Params & params, ref<LocalStore> next, LocalDerivationGoal & goal)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RestrictedStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , next(next), goal(goal)
    { }

    Path getRealStoreDir() override
    { return next->realStoreDir; }

    std::string getUri() override
    { return next->getUri(); }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;
        for (auto & p : goal.inputPaths) paths.insert(p);
        for (auto & p : goal.addedPaths) paths.insert(p);
        return paths;
    }

    std::shared_ptr<const ValidPathInfo> queryPathInfoUncached(const StorePath & path) override
    {
        if (goal.isAllowed(path)) {
            try {
                /* Censor impure information. */
                auto info = std::make_shared<ValidPathInfo>(*next->queryPathInfo(path));
                info->deriver.reset();
                info->registrationTime = 0;
                info->ultimate = false;
                info->sigs.clear();
                return info;
            } catch (InvalidPath &) {
                return nullptr;
            }
        } else
            return nullptr;
    };

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override
    { }

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(
        const StorePath & path,
        Store * evalStore = nullptr) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot query output map for unknown path '%s' in recursive Nix", printStorePath(path));
        return next->queryPartialDerivationOutputMap(path, evalStore);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { throw Error("queryPathFromHashPart"); }

    StorePath addToStore(
        std::string_view name,
        const Path & srcPath,
        FileIngestionMethod method,
        HashType hashAlgo,
        PathFilter & filter,
        RepairFlag repair,
        const StorePathSet & references) override
    { throw Error("addToStore"); }

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs) override
    {
        next->addToStore(info, narSource, repair, checkSigs);
        goal.addDependency(info.path);
    }

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair = NoRepair) override
    {
        auto path = next->addTextToStore(name, s, references, repair);
        goal.addDependency(path);
        return path;
    }

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileIngestionMethod method,
        HashType hashAlgo,
        RepairFlag repair,
        const StorePathSet & references) override
    {
        auto path = next->addToStoreFromDump(dump, name, method, hashAlgo, repair, references);
        goal.addDependency(path);
        return path;
    }

    WireFormatGenerator narFromPath(const StorePath & path) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot dump unknown path '%s' in recursive Nix", printStorePath(path));
        return LocalFSStore::narFromPath(path);
    }

    void ensurePath(const StorePath & path) override
    {
        if (!goal.isAllowed(path))
            throw InvalidPath("cannot substitute unknown path '%s' in recursive Nix", printStorePath(path));
        /* Nothing to be done; 'path' must already be valid. */
    }

    void registerDrvOutput(const Realisation & info) override
    // XXX: This should probably be allowed as a no-op if the realisation
    // corresponds to an allowed derivation
    { throw Error("registerDrvOutput"); }

    std::shared_ptr<const Realisation> queryRealisationUncached(const DrvOutput & id) override
    // XXX: This should probably be allowed if the realisation corresponds to
    // an allowed derivation
    {
        if (!goal.isAllowed(id))
            return nullptr;
        return next->queryRealisation(id);
    }

    void buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override
    {
        for (auto & result : buildPathsWithResults(paths, buildMode, evalStore))
            if (!result.success())
                result.rethrow();
    }

    std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr) override
    {
        assert(!evalStore);

        if (buildMode != bmNormal) throw Error("unsupported build mode");

        StorePathSet newPaths;
        std::set<Realisation> newRealisations;

        for (auto & req : paths) {
            if (!goal.isAllowed(req))
                throw InvalidPath("cannot build '%s' in recursive Nix because path is unknown", req.to_string(*next));
        }

        auto results = next->buildPathsWithResults(paths, buildMode);

        for (auto & result : results) {
            for (auto & [outputName, output] : result.builtOutputs) {
                newPaths.insert(output.outPath);
                newRealisations.insert(output);
            }
        }

        StorePathSet closure;
        next->computeFSClosure(newPaths, closure);
        for (auto & path : closure)
            goal.addDependency(path);
        for (auto & real : Realisation::closure(*next, newRealisations))
            goal.addedDrvOutputs.insert(real.id);

        return results;
    }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) override
    { unsupported("buildDerivation"); }

    void addTempRoot(const StorePath & path) override
    { }

    void addIndirectRoot(const Path & path) override
    { }

    Roots findRoots(bool censor) override
    { return Roots(); }

    void collectGarbage(const GCOptions & options, GCResults & results) override
    { }

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override
    { unsupported("addSignatures"); }

    void queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize) override
    {
        /* This is slightly impure since it leaks information to the
           client about what paths will be built/substituted or are
           already present. Probably not a big deal. */

        std::vector<DerivedPath> allowed;
        for (auto & req : targets) {
            if (goal.isAllowed(req))
                allowed.emplace_back(req);
            else
                unknown.insert(pathPartOfReq(req));
        }

        next->queryMissing(allowed, willBuild, willSubstitute,
            unknown, downloadSize, narSize);
    }

    virtual std::optional<std::string> getBuildLogExact(const StorePath & path) override
    { return std::nullopt; }

    virtual void addBuildLog(const StorePath & path, std::string_view log) override
    { unsupported("addBuildLog"); }

    std::optional<TrustedFlag> isTrustedClient() override
    { return NotTrusted; }
};


void LocalDerivationGoal::startDaemon()
{
    experimentalFeatureSettings.require(Xp::RecursiveNix);

    Store::Params params;
    params["path-info-cache-size"] = "0";
    params["store"] = worker.store.storeDir;
    if (auto & optRoot = getLocalStore().rootDir.get())
        params["root"] = *optRoot;
    params["state"] = "/no-such-path";
    params["log"] = "/no-such-path";
    auto store = make_ref<RestrictedStore>(params,
        ref<LocalStore>(std::dynamic_pointer_cast<LocalStore>(worker.store.shared_from_this())),
        *this);

    addedPaths.clear();

    auto socketName = ".nix-socket";
    Path socketPath = tmpDir + "/" + socketName;
    env["NIX_REMOTE"] = "unix://" + tmpDirInSandbox + "/" + socketName;

    daemonSocket = createUnixDomainSocket(socketPath, 0600);

    chownToBuilder(socketPath);

    daemonThread = std::thread([this, store]() {

        while (true) {

            /* Accept a connection. */
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote{accept(daemonSocket.get(),
                (struct sockaddr *) &remoteAddr, &remoteAddrLen)};
            if (!remote) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (errno == EINVAL || errno == ECONNABORTED) break;
                throw SysError("accepting connection");
            }

            closeOnExec(remote.get());

            debug("received daemon connection");

            auto workerThread = std::thread([store, remote{std::move(remote)}]() {
                FdSource from(remote.get());
                FdSink to(remote.get());
                try {
                    daemon::processConnection(store, from, to,
                        NotTrusted, daemon::Recursive);
                    debug("terminated daemon connection");
                } catch (SysError &) {
                    ignoreException();
                }
            });

            daemonWorkerThreads.push_back(std::move(workerThread));
        }

        debug("daemon shutting down");
    });
}


void LocalDerivationGoal::stopDaemon()
{
    if (daemonSocket && shutdown(daemonSocket.get(), SHUT_RDWR) == -1) {
        // According to the POSIX standard, the 'shutdown' function should
        // return an ENOTCONN error when attempting to shut down a socket that
        // hasn't been connected yet. This situation occurs when the 'accept'
        // function is called on a socket without any accepted connections,
        // leaving the socket unconnected. While Linux doesn't seem to produce
        // an error for sockets that have only been accepted, more
        // POSIX-compliant operating systems like OpenBSD, macOS, and others do
        // return the ENOTCONN error. Therefore, we handle this error here to
        // avoid raising an exception for compliant behaviour.
        if (errno == ENOTCONN) {
            daemonSocket.close();
        } else {
            throw SysError("shutting down daemon socket");
        }
    }

    if (daemonThread.joinable())
        daemonThread.join();

    // FIXME: should prune worker threads more quickly.
    // FIXME: shutdown the client socket to speed up worker termination.
    for (auto & thread : daemonWorkerThreads)
        thread.join();
    daemonWorkerThreads.clear();

    // release the socket.
    daemonSocket.close();
}


void LocalDerivationGoal::addDependency(const StorePath & path)
{
    if (isAllowed(path)) return;

    addedPaths.insert(path);

    /* If we're doing a sandbox build, then we have to make the path
       appear in the sandbox. */
    if (useChroot) {

        debug("materialising '%s' in the sandbox", worker.store.printStorePath(path));

        #if __linux__

            Path source = worker.store.Store::toRealPath(path);
            Path target = chrootRootDir + worker.store.printStorePath(path);

            if (pathExists(target)) {
                // There is a similar debug message in bindPath, so only run it in this block to not have double messages.
                debug("bind-mounting %s -> %s", target, source);
                throw Error("store path '%s' already exists in the sandbox", worker.store.printStorePath(path));
            }

            /* Bind-mount the path into the sandbox. This requires
               entering its mount namespace, which is not possible
               in multithreaded programs. So we do this in a
               child process.*/
            Pid child = startProcess([&]() {

                if (usingUserNamespace && (setns(sandboxUserNamespace.get(), 0) == -1))
                    throw SysError("entering sandbox user namespace");

                if (setns(sandboxMountNamespace.get(), 0) == -1)
                    throw SysError("entering sandbox mount namespace");

                bindPath(source, target);

                _exit(0);
            });

            int status = child.wait();
            if (status != 0)
                throw Error("could not add path '%s' to sandbox", worker.store.printStorePath(path));

        #else
            throw Error("don't know how to make path '%s' (produced by a recursive Nix call) appear in the sandbox",
                worker.store.printStorePath(path));
        #endif

    }
}

void LocalDerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (chown(path.c_str(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of '%1%'", path);
}

#if HAVE_SECCOMP

static void allowSyscall(scmp_filter_ctx ctx, int syscall) {
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0) != 0)
        throw SysError("unable to add seccomp rule");
}

#define ALLOW_CHMOD_IF_SAFE(ctx, syscall, modePos) \
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID | S_ISGID, 0)) != 0 || \
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) != 0 || \
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscall, 1, SCMP_A##modePos(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) != 0) \
        throw SysError("unable to add seccomp rule");

#endif

void setupSeccomp()
{
#if __linux__
#if HAVE_SECCOMP
    scmp_filter_ctx ctx;

    // Pretend that syscalls we don't yet know about don't exist.
    // This is the best option for compatibility: after all, they did in fact not exist not too long ago.
    if (!(ctx = seccomp_init(SCMP_ACT_ERRNO(ENOSYS))))
        throw SysError("unable to initialize seccomp mode 2");

    Finally cleanup([&]() {
        seccomp_release(ctx);
    });

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X86) != 0)
        throw SysError("unable to add 32-bit seccomp architecture");

    if (nativeSystem == "x86_64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_X32) != 0)
        throw SysError("unable to add X32 seccomp architecture");

    if (nativeSystem == "aarch64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_ARM) != 0)
        printError("unable to add ARM seccomp architecture; this may result in spurious build failures if running 32-bit ARM processes");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS) != 0)
        printError("unable to add mips seccomp architecture");

    if (nativeSystem == "mips64-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPS64N32) != 0)
        printError("unable to add mips64-*abin32 seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL) != 0)
        printError("unable to add mipsel seccomp architecture");

    if (nativeSystem == "mips64el-linux" &&
        seccomp_arch_add(ctx, SCMP_ARCH_MIPSEL64N32) != 0)
        printError("unable to add mips64el-*abin32 seccomp architecture");

    // This list is intended for machine consumption.
    // Please keep its format, order and BEGIN/END markers.
    //
    // Currently, it is up to date with libseccomp 2.5.5 and glibc 2.39.
    // Run check-syscalls to determine which new syscalls should be added.
    // New syscalls must be audited and handled in a way that blocks the following dangerous operations:
    // * Creation of non-empty setuid/setgid files
    // * Creation of extended attributes (including ACLs)
    //
    // BEGIN extract-syscalls
    allowSyscall(ctx, SCMP_SYS(accept));
    allowSyscall(ctx, SCMP_SYS(accept4));
    allowSyscall(ctx, SCMP_SYS(access));
    allowSyscall(ctx, SCMP_SYS(acct));
    allowSyscall(ctx, SCMP_SYS(add_key));
    allowSyscall(ctx, SCMP_SYS(adjtimex));
    allowSyscall(ctx, SCMP_SYS(afs_syscall));
    allowSyscall(ctx, SCMP_SYS(alarm));
    allowSyscall(ctx, SCMP_SYS(arch_prctl));
    allowSyscall(ctx, SCMP_SYS(arm_fadvise64_64));
    allowSyscall(ctx, SCMP_SYS(arm_sync_file_range));
    allowSyscall(ctx, SCMP_SYS(bdflush));
    allowSyscall(ctx, SCMP_SYS(bind));
    allowSyscall(ctx, SCMP_SYS(bpf));
    allowSyscall(ctx, SCMP_SYS(break));
    allowSyscall(ctx, SCMP_SYS(breakpoint));
    allowSyscall(ctx, SCMP_SYS(brk));
    allowSyscall(ctx, SCMP_SYS(cachectl));
    allowSyscall(ctx, SCMP_SYS(cacheflush));
    allowSyscall(ctx, SCMP_SYS(cachestat));
    allowSyscall(ctx, SCMP_SYS(capget));
    allowSyscall(ctx, SCMP_SYS(capset));
    allowSyscall(ctx, SCMP_SYS(chdir));
    // skip chmod (dangerous)
    allowSyscall(ctx, SCMP_SYS(chown));
    allowSyscall(ctx, SCMP_SYS(chown32));
    allowSyscall(ctx, SCMP_SYS(chroot));
    allowSyscall(ctx, SCMP_SYS(clock_adjtime));
    allowSyscall(ctx, SCMP_SYS(clock_adjtime64));
    allowSyscall(ctx, SCMP_SYS(clock_getres));
    allowSyscall(ctx, SCMP_SYS(clock_getres_time64));
    allowSyscall(ctx, SCMP_SYS(clock_gettime));
    allowSyscall(ctx, SCMP_SYS(clock_gettime64));
    allowSyscall(ctx, SCMP_SYS(clock_nanosleep));
    allowSyscall(ctx, SCMP_SYS(clock_nanosleep_time64));
    allowSyscall(ctx, SCMP_SYS(clock_settime));
    allowSyscall(ctx, SCMP_SYS(clock_settime64));
    allowSyscall(ctx, SCMP_SYS(clone));
    allowSyscall(ctx, SCMP_SYS(clone3));
    allowSyscall(ctx, SCMP_SYS(close));
    allowSyscall(ctx, SCMP_SYS(close_range));
    allowSyscall(ctx, SCMP_SYS(connect));
    allowSyscall(ctx, SCMP_SYS(copy_file_range));
    allowSyscall(ctx, SCMP_SYS(creat));
    allowSyscall(ctx, SCMP_SYS(create_module));
    allowSyscall(ctx, SCMP_SYS(delete_module));
    allowSyscall(ctx, SCMP_SYS(dup));
    allowSyscall(ctx, SCMP_SYS(dup2));
    allowSyscall(ctx, SCMP_SYS(dup3));
    allowSyscall(ctx, SCMP_SYS(epoll_create));
    allowSyscall(ctx, SCMP_SYS(epoll_create1));
    allowSyscall(ctx, SCMP_SYS(epoll_ctl));
    allowSyscall(ctx, SCMP_SYS(epoll_ctl_old));
    allowSyscall(ctx, SCMP_SYS(epoll_pwait));
    allowSyscall(ctx, SCMP_SYS(epoll_pwait2));
    allowSyscall(ctx, SCMP_SYS(epoll_wait));
    allowSyscall(ctx, SCMP_SYS(epoll_wait_old));
    allowSyscall(ctx, SCMP_SYS(eventfd));
    allowSyscall(ctx, SCMP_SYS(eventfd2));
    allowSyscall(ctx, SCMP_SYS(execve));
    allowSyscall(ctx, SCMP_SYS(execveat));
    allowSyscall(ctx, SCMP_SYS(exit));
    allowSyscall(ctx, SCMP_SYS(exit_group));
    allowSyscall(ctx, SCMP_SYS(faccessat));
    allowSyscall(ctx, SCMP_SYS(faccessat2));
    allowSyscall(ctx, SCMP_SYS(fadvise64));
    allowSyscall(ctx, SCMP_SYS(fadvise64_64));
    allowSyscall(ctx, SCMP_SYS(fallocate));
    allowSyscall(ctx, SCMP_SYS(fanotify_init));
    allowSyscall(ctx, SCMP_SYS(fanotify_mark));
    allowSyscall(ctx, SCMP_SYS(fchdir));
    // skip fchmod (dangerous)
    // skip fchmodat (dangerous)
    // skip fchmodat2 (dangerous)
    allowSyscall(ctx, SCMP_SYS(fchown));
    allowSyscall(ctx, SCMP_SYS(fchown32));
    allowSyscall(ctx, SCMP_SYS(fchownat));
    allowSyscall(ctx, SCMP_SYS(fcntl));
    allowSyscall(ctx, SCMP_SYS(fcntl64));
    allowSyscall(ctx, SCMP_SYS(fdatasync));
    allowSyscall(ctx, SCMP_SYS(fgetxattr));
    allowSyscall(ctx, SCMP_SYS(finit_module));
    allowSyscall(ctx, SCMP_SYS(flistxattr));
    allowSyscall(ctx, SCMP_SYS(flock));
    allowSyscall(ctx, SCMP_SYS(fork));
    allowSyscall(ctx, SCMP_SYS(fremovexattr));
    allowSyscall(ctx, SCMP_SYS(fsconfig));
    // skip fsetxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(fsmount));
    allowSyscall(ctx, SCMP_SYS(fsopen));
    allowSyscall(ctx, SCMP_SYS(fspick));
    allowSyscall(ctx, SCMP_SYS(fstat));
    allowSyscall(ctx, SCMP_SYS(fstat64));
    allowSyscall(ctx, SCMP_SYS(fstatat64));
    allowSyscall(ctx, SCMP_SYS(fstatfs));
    allowSyscall(ctx, SCMP_SYS(fstatfs64));
    allowSyscall(ctx, SCMP_SYS(fsync));
    allowSyscall(ctx, SCMP_SYS(ftime));
    allowSyscall(ctx, SCMP_SYS(ftruncate));
    allowSyscall(ctx, SCMP_SYS(ftruncate64));
    allowSyscall(ctx, SCMP_SYS(futex));
    allowSyscall(ctx, SCMP_SYS(futex_requeue));
    allowSyscall(ctx, SCMP_SYS(futex_time64));
    allowSyscall(ctx, SCMP_SYS(futex_wait));
    allowSyscall(ctx, SCMP_SYS(futex_waitv));
    allowSyscall(ctx, SCMP_SYS(futex_wake));
    allowSyscall(ctx, SCMP_SYS(futimesat));
    allowSyscall(ctx, SCMP_SYS(getcpu));
    allowSyscall(ctx, SCMP_SYS(getcwd));
    allowSyscall(ctx, SCMP_SYS(getdents));
    allowSyscall(ctx, SCMP_SYS(getdents64));
    allowSyscall(ctx, SCMP_SYS(getegid));
    allowSyscall(ctx, SCMP_SYS(getegid32));
    allowSyscall(ctx, SCMP_SYS(geteuid));
    allowSyscall(ctx, SCMP_SYS(geteuid32));
    allowSyscall(ctx, SCMP_SYS(getgid));
    allowSyscall(ctx, SCMP_SYS(getgid32));
    allowSyscall(ctx, SCMP_SYS(getgroups));
    allowSyscall(ctx, SCMP_SYS(getgroups32));
    allowSyscall(ctx, SCMP_SYS(getitimer));
    allowSyscall(ctx, SCMP_SYS(get_kernel_syms));
    allowSyscall(ctx, SCMP_SYS(get_mempolicy));
    allowSyscall(ctx, SCMP_SYS(getpeername));
    allowSyscall(ctx, SCMP_SYS(getpgid));
    allowSyscall(ctx, SCMP_SYS(getpgrp));
    allowSyscall(ctx, SCMP_SYS(getpid));
    allowSyscall(ctx, SCMP_SYS(getpmsg));
    allowSyscall(ctx, SCMP_SYS(getppid));
    allowSyscall(ctx, SCMP_SYS(getpriority));
    allowSyscall(ctx, SCMP_SYS(getrandom));
    allowSyscall(ctx, SCMP_SYS(getresgid));
    allowSyscall(ctx, SCMP_SYS(getresgid32));
    allowSyscall(ctx, SCMP_SYS(getresuid));
    allowSyscall(ctx, SCMP_SYS(getresuid32));
    allowSyscall(ctx, SCMP_SYS(getrlimit));
    allowSyscall(ctx, SCMP_SYS(get_robust_list));
    allowSyscall(ctx, SCMP_SYS(getrusage));
    allowSyscall(ctx, SCMP_SYS(getsid));
    allowSyscall(ctx, SCMP_SYS(getsockname));
    allowSyscall(ctx, SCMP_SYS(getsockopt));
    allowSyscall(ctx, SCMP_SYS(get_thread_area));
    allowSyscall(ctx, SCMP_SYS(gettid));
    allowSyscall(ctx, SCMP_SYS(gettimeofday));
    allowSyscall(ctx, SCMP_SYS(get_tls));
    allowSyscall(ctx, SCMP_SYS(getuid));
    allowSyscall(ctx, SCMP_SYS(getuid32));
    allowSyscall(ctx, SCMP_SYS(getxattr));
    allowSyscall(ctx, SCMP_SYS(gtty));
    allowSyscall(ctx, SCMP_SYS(idle));
    allowSyscall(ctx, SCMP_SYS(init_module));
    allowSyscall(ctx, SCMP_SYS(inotify_add_watch));
    allowSyscall(ctx, SCMP_SYS(inotify_init));
    allowSyscall(ctx, SCMP_SYS(inotify_init1));
    allowSyscall(ctx, SCMP_SYS(inotify_rm_watch));
    allowSyscall(ctx, SCMP_SYS(io_cancel));
    allowSyscall(ctx, SCMP_SYS(ioctl));
    allowSyscall(ctx, SCMP_SYS(io_destroy));
    allowSyscall(ctx, SCMP_SYS(io_getevents));
    allowSyscall(ctx, SCMP_SYS(ioperm));
    allowSyscall(ctx, SCMP_SYS(io_pgetevents));
    allowSyscall(ctx, SCMP_SYS(io_pgetevents_time64));
    allowSyscall(ctx, SCMP_SYS(iopl));
    allowSyscall(ctx, SCMP_SYS(ioprio_get));
    allowSyscall(ctx, SCMP_SYS(ioprio_set));
    allowSyscall(ctx, SCMP_SYS(io_setup));
    allowSyscall(ctx, SCMP_SYS(io_submit));
    // skip io_uring_enter (may become dangerous)
    // skip io_uring_register (may become dangerous)
    // skip io_uring_setup (may become dangerous)
    allowSyscall(ctx, SCMP_SYS(ipc));
    allowSyscall(ctx, SCMP_SYS(kcmp));
    allowSyscall(ctx, SCMP_SYS(kexec_file_load));
    allowSyscall(ctx, SCMP_SYS(kexec_load));
    allowSyscall(ctx, SCMP_SYS(keyctl));
    allowSyscall(ctx, SCMP_SYS(kill));
    allowSyscall(ctx, SCMP_SYS(landlock_add_rule));
    allowSyscall(ctx, SCMP_SYS(landlock_create_ruleset));
    allowSyscall(ctx, SCMP_SYS(landlock_restrict_self));
    allowSyscall(ctx, SCMP_SYS(lchown));
    allowSyscall(ctx, SCMP_SYS(lchown32));
    allowSyscall(ctx, SCMP_SYS(lgetxattr));
    allowSyscall(ctx, SCMP_SYS(link));
    allowSyscall(ctx, SCMP_SYS(linkat));
    allowSyscall(ctx, SCMP_SYS(listen));
    allowSyscall(ctx, SCMP_SYS(listxattr));
    allowSyscall(ctx, SCMP_SYS(llistxattr));
    allowSyscall(ctx, SCMP_SYS(_llseek));
    allowSyscall(ctx, SCMP_SYS(lock));
    allowSyscall(ctx, SCMP_SYS(lookup_dcookie));
    allowSyscall(ctx, SCMP_SYS(lremovexattr));
    allowSyscall(ctx, SCMP_SYS(lseek));
    // skip lsetxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(lstat));
    allowSyscall(ctx, SCMP_SYS(lstat64));
    allowSyscall(ctx, SCMP_SYS(madvise));
    allowSyscall(ctx, SCMP_SYS(map_shadow_stack));
    allowSyscall(ctx, SCMP_SYS(mbind));
    allowSyscall(ctx, SCMP_SYS(membarrier));
    allowSyscall(ctx, SCMP_SYS(memfd_create));
    allowSyscall(ctx, SCMP_SYS(memfd_secret));
    allowSyscall(ctx, SCMP_SYS(migrate_pages));
    allowSyscall(ctx, SCMP_SYS(mincore));
    allowSyscall(ctx, SCMP_SYS(mkdir));
    allowSyscall(ctx, SCMP_SYS(mkdirat));
    allowSyscall(ctx, SCMP_SYS(mknod));
    allowSyscall(ctx, SCMP_SYS(mknodat));
    allowSyscall(ctx, SCMP_SYS(mlock));
    allowSyscall(ctx, SCMP_SYS(mlock2));
    allowSyscall(ctx, SCMP_SYS(mlockall));
    allowSyscall(ctx, SCMP_SYS(mmap));
    allowSyscall(ctx, SCMP_SYS(mmap2));
    allowSyscall(ctx, SCMP_SYS(modify_ldt));
    allowSyscall(ctx, SCMP_SYS(mount));
    allowSyscall(ctx, SCMP_SYS(mount_setattr));
    allowSyscall(ctx, SCMP_SYS(move_mount));
    allowSyscall(ctx, SCMP_SYS(move_pages));
    allowSyscall(ctx, SCMP_SYS(mprotect));
    allowSyscall(ctx, SCMP_SYS(mpx));
    allowSyscall(ctx, SCMP_SYS(mq_getsetattr));
    allowSyscall(ctx, SCMP_SYS(mq_notify));
    allowSyscall(ctx, SCMP_SYS(mq_open));
    allowSyscall(ctx, SCMP_SYS(mq_timedreceive));
    allowSyscall(ctx, SCMP_SYS(mq_timedreceive_time64));
    allowSyscall(ctx, SCMP_SYS(mq_timedsend));
    allowSyscall(ctx, SCMP_SYS(mq_timedsend_time64));
    allowSyscall(ctx, SCMP_SYS(mq_unlink));
    allowSyscall(ctx, SCMP_SYS(mremap));
    allowSyscall(ctx, SCMP_SYS(msgctl));
    allowSyscall(ctx, SCMP_SYS(msgget));
    allowSyscall(ctx, SCMP_SYS(msgrcv));
    allowSyscall(ctx, SCMP_SYS(msgsnd));
    allowSyscall(ctx, SCMP_SYS(msync));
    allowSyscall(ctx, SCMP_SYS(multiplexer));
    allowSyscall(ctx, SCMP_SYS(munlock));
    allowSyscall(ctx, SCMP_SYS(munlockall));
    allowSyscall(ctx, SCMP_SYS(munmap));
    allowSyscall(ctx, SCMP_SYS(name_to_handle_at));
    allowSyscall(ctx, SCMP_SYS(nanosleep));
    allowSyscall(ctx, SCMP_SYS(newfstatat));
    allowSyscall(ctx, SCMP_SYS(_newselect));
    allowSyscall(ctx, SCMP_SYS(nfsservctl));
    allowSyscall(ctx, SCMP_SYS(nice));
    allowSyscall(ctx, SCMP_SYS(oldfstat));
    allowSyscall(ctx, SCMP_SYS(oldlstat));
    allowSyscall(ctx, SCMP_SYS(oldolduname));
    allowSyscall(ctx, SCMP_SYS(oldstat));
    allowSyscall(ctx, SCMP_SYS(olduname));
    allowSyscall(ctx, SCMP_SYS(open));
    allowSyscall(ctx, SCMP_SYS(openat));
    allowSyscall(ctx, SCMP_SYS(openat2));
    allowSyscall(ctx, SCMP_SYS(open_by_handle_at));
    allowSyscall(ctx, SCMP_SYS(open_tree));
    allowSyscall(ctx, SCMP_SYS(pause));
    allowSyscall(ctx, SCMP_SYS(pciconfig_iobase));
    allowSyscall(ctx, SCMP_SYS(pciconfig_read));
    allowSyscall(ctx, SCMP_SYS(pciconfig_write));
    allowSyscall(ctx, SCMP_SYS(perf_event_open));
    allowSyscall(ctx, SCMP_SYS(personality));
    allowSyscall(ctx, SCMP_SYS(pidfd_getfd));
    allowSyscall(ctx, SCMP_SYS(pidfd_open));
    allowSyscall(ctx, SCMP_SYS(pidfd_send_signal));
    allowSyscall(ctx, SCMP_SYS(pipe));
    allowSyscall(ctx, SCMP_SYS(pipe2));
    allowSyscall(ctx, SCMP_SYS(pivot_root));
    allowSyscall(ctx, SCMP_SYS(pkey_alloc));
    allowSyscall(ctx, SCMP_SYS(pkey_free));
    allowSyscall(ctx, SCMP_SYS(pkey_mprotect));
    allowSyscall(ctx, SCMP_SYS(poll));
    allowSyscall(ctx, SCMP_SYS(ppoll));
    allowSyscall(ctx, SCMP_SYS(ppoll_time64));
    allowSyscall(ctx, SCMP_SYS(prctl));
    allowSyscall(ctx, SCMP_SYS(pread64));
    allowSyscall(ctx, SCMP_SYS(preadv));
    allowSyscall(ctx, SCMP_SYS(preadv2));
    allowSyscall(ctx, SCMP_SYS(prlimit64));
    allowSyscall(ctx, SCMP_SYS(process_madvise));
    allowSyscall(ctx, SCMP_SYS(process_mrelease));
    allowSyscall(ctx, SCMP_SYS(process_vm_readv));
    allowSyscall(ctx, SCMP_SYS(process_vm_writev));
    allowSyscall(ctx, SCMP_SYS(prof));
    allowSyscall(ctx, SCMP_SYS(profil));
    allowSyscall(ctx, SCMP_SYS(pselect6));
    allowSyscall(ctx, SCMP_SYS(pselect6_time64));
    allowSyscall(ctx, SCMP_SYS(ptrace));
    allowSyscall(ctx, SCMP_SYS(putpmsg));
    allowSyscall(ctx, SCMP_SYS(pwrite64));
    allowSyscall(ctx, SCMP_SYS(pwritev));
    allowSyscall(ctx, SCMP_SYS(pwritev2));
    allowSyscall(ctx, SCMP_SYS(query_module));
    allowSyscall(ctx, SCMP_SYS(quotactl));
    allowSyscall(ctx, SCMP_SYS(quotactl_fd));
    allowSyscall(ctx, SCMP_SYS(read));
    allowSyscall(ctx, SCMP_SYS(readahead));
    allowSyscall(ctx, SCMP_SYS(readdir));
    allowSyscall(ctx, SCMP_SYS(readlink));
    allowSyscall(ctx, SCMP_SYS(readlinkat));
    allowSyscall(ctx, SCMP_SYS(readv));
    allowSyscall(ctx, SCMP_SYS(reboot));
    allowSyscall(ctx, SCMP_SYS(recv));
    allowSyscall(ctx, SCMP_SYS(recvfrom));
    allowSyscall(ctx, SCMP_SYS(recvmmsg));
    allowSyscall(ctx, SCMP_SYS(recvmmsg_time64));
    allowSyscall(ctx, SCMP_SYS(recvmsg));
    allowSyscall(ctx, SCMP_SYS(remap_file_pages));
    allowSyscall(ctx, SCMP_SYS(removexattr));
    allowSyscall(ctx, SCMP_SYS(rename));
    allowSyscall(ctx, SCMP_SYS(renameat));
    allowSyscall(ctx, SCMP_SYS(renameat2));
    allowSyscall(ctx, SCMP_SYS(request_key));
    allowSyscall(ctx, SCMP_SYS(restart_syscall));
    allowSyscall(ctx, SCMP_SYS(riscv_flush_icache));
    allowSyscall(ctx, SCMP_SYS(rmdir));
    allowSyscall(ctx, SCMP_SYS(rseq));
    allowSyscall(ctx, SCMP_SYS(rtas));
    allowSyscall(ctx, SCMP_SYS(rt_sigaction));
    allowSyscall(ctx, SCMP_SYS(rt_sigpending));
    allowSyscall(ctx, SCMP_SYS(rt_sigprocmask));
    allowSyscall(ctx, SCMP_SYS(rt_sigqueueinfo));
    allowSyscall(ctx, SCMP_SYS(rt_sigreturn));
    allowSyscall(ctx, SCMP_SYS(rt_sigsuspend));
    allowSyscall(ctx, SCMP_SYS(rt_sigtimedwait));
    allowSyscall(ctx, SCMP_SYS(rt_sigtimedwait_time64));
    allowSyscall(ctx, SCMP_SYS(rt_tgsigqueueinfo));
    allowSyscall(ctx, SCMP_SYS(s390_guarded_storage));
    allowSyscall(ctx, SCMP_SYS(s390_pci_mmio_read));
    allowSyscall(ctx, SCMP_SYS(s390_pci_mmio_write));
    allowSyscall(ctx, SCMP_SYS(s390_runtime_instr));
    allowSyscall(ctx, SCMP_SYS(s390_sthyi));
    allowSyscall(ctx, SCMP_SYS(sched_getaffinity));
    allowSyscall(ctx, SCMP_SYS(sched_getattr));
    allowSyscall(ctx, SCMP_SYS(sched_getparam));
    allowSyscall(ctx, SCMP_SYS(sched_get_priority_max));
    allowSyscall(ctx, SCMP_SYS(sched_get_priority_min));
    allowSyscall(ctx, SCMP_SYS(sched_getscheduler));
    allowSyscall(ctx, SCMP_SYS(sched_rr_get_interval));
    allowSyscall(ctx, SCMP_SYS(sched_rr_get_interval_time64));
    allowSyscall(ctx, SCMP_SYS(sched_setaffinity));
    allowSyscall(ctx, SCMP_SYS(sched_setattr));
    allowSyscall(ctx, SCMP_SYS(sched_setparam));
    allowSyscall(ctx, SCMP_SYS(sched_setscheduler));
    allowSyscall(ctx, SCMP_SYS(sched_yield));
    allowSyscall(ctx, SCMP_SYS(seccomp));
    allowSyscall(ctx, SCMP_SYS(security));
    allowSyscall(ctx, SCMP_SYS(select));
    allowSyscall(ctx, SCMP_SYS(semctl));
    allowSyscall(ctx, SCMP_SYS(semget));
    allowSyscall(ctx, SCMP_SYS(semop));
    allowSyscall(ctx, SCMP_SYS(semtimedop));
    allowSyscall(ctx, SCMP_SYS(semtimedop_time64));
    allowSyscall(ctx, SCMP_SYS(send));
    allowSyscall(ctx, SCMP_SYS(sendfile));
    allowSyscall(ctx, SCMP_SYS(sendfile64));
    allowSyscall(ctx, SCMP_SYS(sendmmsg));
    allowSyscall(ctx, SCMP_SYS(sendmsg));
    allowSyscall(ctx, SCMP_SYS(sendto));
    allowSyscall(ctx, SCMP_SYS(setdomainname));
    allowSyscall(ctx, SCMP_SYS(setfsgid));
    allowSyscall(ctx, SCMP_SYS(setfsgid32));
    allowSyscall(ctx, SCMP_SYS(setfsuid));
    allowSyscall(ctx, SCMP_SYS(setfsuid32));
    allowSyscall(ctx, SCMP_SYS(setgid));
    allowSyscall(ctx, SCMP_SYS(setgid32));
    allowSyscall(ctx, SCMP_SYS(setgroups));
    allowSyscall(ctx, SCMP_SYS(setgroups32));
    allowSyscall(ctx, SCMP_SYS(sethostname));
    allowSyscall(ctx, SCMP_SYS(setitimer));
    allowSyscall(ctx, SCMP_SYS(set_mempolicy));
    allowSyscall(ctx, SCMP_SYS(set_mempolicy_home_node));
    allowSyscall(ctx, SCMP_SYS(setns));
    allowSyscall(ctx, SCMP_SYS(setpgid));
    allowSyscall(ctx, SCMP_SYS(setpriority));
    allowSyscall(ctx, SCMP_SYS(setregid));
    allowSyscall(ctx, SCMP_SYS(setregid32));
    allowSyscall(ctx, SCMP_SYS(setresgid));
    allowSyscall(ctx, SCMP_SYS(setresgid32));
    allowSyscall(ctx, SCMP_SYS(setresuid));
    allowSyscall(ctx, SCMP_SYS(setresuid32));
    allowSyscall(ctx, SCMP_SYS(setreuid));
    allowSyscall(ctx, SCMP_SYS(setreuid32));
    allowSyscall(ctx, SCMP_SYS(setrlimit));
    allowSyscall(ctx, SCMP_SYS(set_robust_list));
    allowSyscall(ctx, SCMP_SYS(setsid));
    allowSyscall(ctx, SCMP_SYS(setsockopt));
    allowSyscall(ctx, SCMP_SYS(set_thread_area));
    allowSyscall(ctx, SCMP_SYS(set_tid_address));
    allowSyscall(ctx, SCMP_SYS(settimeofday));
    allowSyscall(ctx, SCMP_SYS(set_tls));
    allowSyscall(ctx, SCMP_SYS(setuid));
    allowSyscall(ctx, SCMP_SYS(setuid32));
    // skip setxattr (dangerous)
    allowSyscall(ctx, SCMP_SYS(sgetmask));
    allowSyscall(ctx, SCMP_SYS(shmat));
    allowSyscall(ctx, SCMP_SYS(shmctl));
    allowSyscall(ctx, SCMP_SYS(shmdt));
    allowSyscall(ctx, SCMP_SYS(shmget));
    allowSyscall(ctx, SCMP_SYS(shutdown));
    allowSyscall(ctx, SCMP_SYS(sigaction));
    allowSyscall(ctx, SCMP_SYS(sigaltstack));
    allowSyscall(ctx, SCMP_SYS(signal));
    allowSyscall(ctx, SCMP_SYS(signalfd));
    allowSyscall(ctx, SCMP_SYS(signalfd4));
    allowSyscall(ctx, SCMP_SYS(sigpending));
    allowSyscall(ctx, SCMP_SYS(sigprocmask));
    allowSyscall(ctx, SCMP_SYS(sigreturn));
    allowSyscall(ctx, SCMP_SYS(sigsuspend));
    allowSyscall(ctx, SCMP_SYS(socket));
    allowSyscall(ctx, SCMP_SYS(socketcall));
    allowSyscall(ctx, SCMP_SYS(socketpair));
    allowSyscall(ctx, SCMP_SYS(splice));
    allowSyscall(ctx, SCMP_SYS(spu_create));
    allowSyscall(ctx, SCMP_SYS(spu_run));
    allowSyscall(ctx, SCMP_SYS(ssetmask));
    allowSyscall(ctx, SCMP_SYS(stat));
    allowSyscall(ctx, SCMP_SYS(stat64));
    allowSyscall(ctx, SCMP_SYS(statfs));
    allowSyscall(ctx, SCMP_SYS(statfs64));
    allowSyscall(ctx, SCMP_SYS(statx));
    allowSyscall(ctx, SCMP_SYS(stime));
    allowSyscall(ctx, SCMP_SYS(stty));
    allowSyscall(ctx, SCMP_SYS(subpage_prot));
    allowSyscall(ctx, SCMP_SYS(swapcontext));
    allowSyscall(ctx, SCMP_SYS(swapoff));
    allowSyscall(ctx, SCMP_SYS(swapon));
    allowSyscall(ctx, SCMP_SYS(switch_endian));
    allowSyscall(ctx, SCMP_SYS(symlink));
    allowSyscall(ctx, SCMP_SYS(symlinkat));
    allowSyscall(ctx, SCMP_SYS(sync));
    allowSyscall(ctx, SCMP_SYS(sync_file_range));
    allowSyscall(ctx, SCMP_SYS(sync_file_range2));
    allowSyscall(ctx, SCMP_SYS(syncfs));
    allowSyscall(ctx, SCMP_SYS(syscall));
    allowSyscall(ctx, SCMP_SYS(_sysctl));
    allowSyscall(ctx, SCMP_SYS(sys_debug_setcontext));
    allowSyscall(ctx, SCMP_SYS(sysfs));
    allowSyscall(ctx, SCMP_SYS(sysinfo));
    allowSyscall(ctx, SCMP_SYS(syslog));
    allowSyscall(ctx, SCMP_SYS(sysmips));
    allowSyscall(ctx, SCMP_SYS(tee));
    allowSyscall(ctx, SCMP_SYS(tgkill));
    allowSyscall(ctx, SCMP_SYS(time));
    allowSyscall(ctx, SCMP_SYS(timer_create));
    allowSyscall(ctx, SCMP_SYS(timer_delete));
    allowSyscall(ctx, SCMP_SYS(timerfd));
    allowSyscall(ctx, SCMP_SYS(timerfd_create));
    allowSyscall(ctx, SCMP_SYS(timerfd_gettime));
    allowSyscall(ctx, SCMP_SYS(timerfd_gettime64));
    allowSyscall(ctx, SCMP_SYS(timerfd_settime));
    allowSyscall(ctx, SCMP_SYS(timerfd_settime64));
    allowSyscall(ctx, SCMP_SYS(timer_getoverrun));
    allowSyscall(ctx, SCMP_SYS(timer_gettime));
    allowSyscall(ctx, SCMP_SYS(timer_gettime64));
    allowSyscall(ctx, SCMP_SYS(timer_settime));
    allowSyscall(ctx, SCMP_SYS(timer_settime64));
    allowSyscall(ctx, SCMP_SYS(times));
    allowSyscall(ctx, SCMP_SYS(tkill));
    allowSyscall(ctx, SCMP_SYS(truncate));
    allowSyscall(ctx, SCMP_SYS(truncate64));
    allowSyscall(ctx, SCMP_SYS(tuxcall));
    allowSyscall(ctx, SCMP_SYS(ugetrlimit));
    allowSyscall(ctx, SCMP_SYS(ulimit));
    allowSyscall(ctx, SCMP_SYS(umask));
    allowSyscall(ctx, SCMP_SYS(umount));
    allowSyscall(ctx, SCMP_SYS(umount2));
    allowSyscall(ctx, SCMP_SYS(uname));
    allowSyscall(ctx, SCMP_SYS(unlink));
    allowSyscall(ctx, SCMP_SYS(unlinkat));
    allowSyscall(ctx, SCMP_SYS(unshare));
    allowSyscall(ctx, SCMP_SYS(uselib));
    allowSyscall(ctx, SCMP_SYS(userfaultfd));
    allowSyscall(ctx, SCMP_SYS(usr26));
    allowSyscall(ctx, SCMP_SYS(usr32));
    allowSyscall(ctx, SCMP_SYS(ustat));
    allowSyscall(ctx, SCMP_SYS(utime));
    allowSyscall(ctx, SCMP_SYS(utimensat));
    allowSyscall(ctx, SCMP_SYS(utimensat_time64));
    allowSyscall(ctx, SCMP_SYS(utimes));
    allowSyscall(ctx, SCMP_SYS(vfork));
    allowSyscall(ctx, SCMP_SYS(vhangup));
    allowSyscall(ctx, SCMP_SYS(vm86));
    allowSyscall(ctx, SCMP_SYS(vm86old));
    allowSyscall(ctx, SCMP_SYS(vmsplice));
    allowSyscall(ctx, SCMP_SYS(vserver));
    allowSyscall(ctx, SCMP_SYS(wait4));
    allowSyscall(ctx, SCMP_SYS(waitid));
    allowSyscall(ctx, SCMP_SYS(waitpid));
    allowSyscall(ctx, SCMP_SYS(write));
    allowSyscall(ctx, SCMP_SYS(writev));
    // END extract-syscalls

    // chmod family: prevent adding setuid/setgid bits to existing files.
    // The Nix store does not support setuid/setgid, and even their temporary creation can weaken the security of the sandbox.
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(chmod), 1);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmod), 1);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmodat), 2);
    ALLOW_CHMOD_IF_SAFE(ctx, SCMP_SYS(fchmodat2), 2);

    // setxattr family: prevent creation of extended attributes or ACLs.
    // Not all filesystems support them, and they're incompatible with the NAR format.
    if (seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(setxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(lsetxattr), 0) != 0 ||
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOTSUP), SCMP_SYS(fsetxattr), 0) != 0)
        throw SysError("unable to add seccomp rule");

    // Set the NO_NEW_PRIVS prctl flag.
    // This both makes loading seccomp filters work for unprivileged users,
    // and is an additional security measure in its own right.
    if (seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 1) != 0)
        throw SysError("unable to set 'no new privileges' seccomp attribute");

    if (seccomp_load(ctx) != 0)
        throw SysError("unable to load seccomp BPF program");
#else
    // Still set the no-new-privileges flag if libseccomp is not available.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1)
        throw SysError("PR_SET_NO_NEW_PRIVS failed");
#endif
#endif
}


void LocalDerivationGoal::runChild()
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    bool sendException = true;

    try { /* child */

        commonChildInit();

        setupSeccomp();

        bool setUser = true;

        /* Make the contents of netrc available to builtin:fetchurl
           (which may run under a different uid and/or in a sandbox). */
        std::string netrcData;
        try {
            if (drv->isBuiltin() && drv->builder == "builtin:fetchurl" && !derivationType->isSandboxed())
                netrcData = readFile(settings.netrcFile);
        } catch (SysError &) { }

#if __linux__
        if (useChroot) {

            userNamespaceSync.writeSide.reset();

            if (drainFD(userNamespaceSync.readSide.get()) != "1")
                throw Error("user namespace initialisation failed");

            userNamespaceSync.readSide.reset();

            if (privateNetwork) {

                /* Initialise the loopback interface. */
                AutoCloseFD fd(socket(PF_INET, SOCK_DGRAM, IPPROTO_IP));
                if (!fd) throw SysError("cannot open IP socket");

                struct ifreq ifr;
                strcpy(ifr.ifr_name, "lo");
                ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
                if (ioctl(fd.get(), SIOCSIFFLAGS, &ifr) == -1)
                    throw SysError("cannot set loopback interface flags");
            }

            /* Set the hostname etc. to fixed values. */
            char hostname[] = "localhost";
            if (sethostname(hostname, sizeof(hostname)) == -1)
                throw SysError("cannot set host name");
            char domainname[] = "(none)"; // kernel default
            if (setdomainname(domainname, sizeof(domainname)) == -1)
                throw SysError("cannot set domain name");

            /* Make all filesystems private.  This is necessary
               because subtrees may have been mounted as "shared"
               (MS_SHARED).  (Systemd does this, for instance.)  Even
               though we have a private mount namespace, mounting
               filesystems on top of a shared subtree still propagates
               outside of the namespace.  Making a subtree private is
               local to the namespace, though, so setting MS_PRIVATE
               does not affect the outside world. */
            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                throw SysError("unable to make '/' private");

            /* Bind-mount chroot directory to itself, to treat it as a
               different filesystem from /, as needed for pivot_root. */
            if (mount(chrootRootDir.c_str(), chrootRootDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount '%1%'", chrootRootDir);

            /* Bind-mount the sandbox's Nix store onto itself so that
               we can mark it as a "shared" subtree, allowing bind
               mounts made in *this* mount namespace to be propagated
               into the child namespace created by the
               unshare(CLONE_NEWNS) call below.

               Marking chrootRootDir as MS_SHARED causes pivot_root()
               to fail with EINVAL. Don't know why. */
            Path chrootStoreDir = chrootRootDir + worker.store.storeDir;

            if (mount(chrootStoreDir.c_str(), chrootStoreDir.c_str(), 0, MS_BIND, 0) == -1)
                throw SysError("unable to bind mount the Nix store", chrootStoreDir);

            if (mount(0, chrootStoreDir.c_str(), 0, MS_SHARED, 0) == -1)
                throw SysError("unable to make '%s' shared", chrootStoreDir);

            /* Set up a nearly empty /dev, unless the user asked to
               bind-mount the host /dev. */
            Strings ss;
            if (pathsInChroot.find("/dev") == pathsInChroot.end()) {
                createDirs(chrootRootDir + "/dev/shm");
                createDirs(chrootRootDir + "/dev/pts");
                ss.push_back("/dev/full");
                if (worker.store.systemFeatures.get().count("kvm") && pathExists("/dev/kvm"))
                    ss.push_back("/dev/kvm");
                ss.push_back("/dev/null");
                ss.push_back("/dev/random");
                ss.push_back("/dev/tty");
                ss.push_back("/dev/urandom");
                ss.push_back("/dev/zero");
                createSymlink("/proc/self/fd", chrootRootDir + "/dev/fd");
                createSymlink("/proc/self/fd/0", chrootRootDir + "/dev/stdin");
                createSymlink("/proc/self/fd/1", chrootRootDir + "/dev/stdout");
                createSymlink("/proc/self/fd/2", chrootRootDir + "/dev/stderr");
            }

            /* Fixed-output derivations typically need to access the
               network, so give them access to /etc/resolv.conf and so
               on. */
            if (!derivationType->isSandboxed()) {
                // Only use nss functions to resolve hosts and
                // services. Don’t use it for anything else that may
                // be configured for this system. This limits the
                // potential impurities introduced in fixed-outputs.
                writeFile(chrootRootDir + "/etc/nsswitch.conf", "hosts: files dns\nservices: files\n");

                /* N.B. it is realistic that these paths might not exist. It
                   happens when testing Nix building fixed-output derivations
                   within a pure derivation. */
                for (auto & path : { "/etc/resolv.conf", "/etc/services", "/etc/hosts" })
                    if (pathExists(path)) {
                        // Copy the actual file, not the symlink, because we don't know where
                        // the symlink is pointing, and we don't want to chase down the entire
                        // chain.
                        //
                        // This means if your network config changes during a FOD build,
                        // the DNS in the sandbox will be wrong. However, this is pretty unlikely
                        // to actually be a problem, because FODs are generally pretty fast,
                        // and machines with often-changing network configurations probably
                        // want to run resolved or some other local resolver anyway.
                        //
                        // There's also just no simple way to do this correctly, you have to manually
                        // inotify watch the files for changes on the outside and update the sandbox
                        // while the build is running (or at least that's what Flatpak does).
                        //
                        // I also just generally feel icky about modifying sandbox state under a build,
                        // even though it really shouldn't be a big deal. -K900
                        copyFile(path, chrootRootDir + path, { .followSymlinks = true });
                    }

                if (settings.caFile != "" && pathExists(settings.caFile)) {
                    // For the same reasons as above, copy the CA certificates file too.
                    // It should be even less likely to change during the build than resolv.conf.
                    createDirs(chrootRootDir + "/etc/ssl/certs");
                    copyFile(settings.caFile, chrootRootDir + "/etc/ssl/certs/ca-certificates.crt", { .followSymlinks = true });
                }
            }

            for (auto & i : ss) pathsInChroot.emplace(i, i);

            /* Bind-mount all the directories from the "host"
               filesystem that we want in the chroot
               environment. */
            for (auto & i : pathsInChroot) {
                if (i.second.source == "/proc") continue; // backwards compatibility

                #if HAVE_EMBEDDED_SANDBOX_SHELL
                if (i.second.source == "__embedded_sandbox_shell__") {
                    static unsigned char sh[] = {
                        #include "embedded-sandbox-shell.gen.hh"
                    };
                    auto dst = chrootRootDir + i.first;
                    createDirs(dirOf(dst));
                    writeFile(dst, std::string_view((const char *) sh, sizeof(sh)));
                    chmodPath(dst, 0555);
                } else
                #endif
                    bindPath(i.second.source, chrootRootDir + i.first, i.second.optional);
            }

            /* Bind a new instance of procfs on /proc. */
            createDirs(chrootRootDir + "/proc");
            if (mount("none", (chrootRootDir + "/proc").c_str(), "proc", 0, 0) == -1)
                throw SysError("mounting /proc");

            /* Mount sysfs on /sys. */
            if (buildUser && buildUser->getUIDCount() != 1) {
                createDirs(chrootRootDir + "/sys");
                if (mount("none", (chrootRootDir + "/sys").c_str(), "sysfs", 0, 0) == -1)
                    throw SysError("mounting /sys");
            }

            /* Mount a new tmpfs on /dev/shm to ensure that whatever
               the builder puts in /dev/shm is cleaned up automatically. */
            if (pathExists("/dev/shm") && mount("none", (chrootRootDir + "/dev/shm").c_str(), "tmpfs", 0,
                    fmt("size=%s", settings.sandboxShmSize).c_str()) == -1)
                throw SysError("mounting /dev/shm");

            /* Mount a new devpts on /dev/pts.  Note that this
               requires the kernel to be compiled with
               CONFIG_DEVPTS_MULTIPLE_INSTANCES=y (which is the case
               if /dev/ptx/ptmx exists). */
            if (pathExists("/dev/pts/ptmx") &&
                !pathExists(chrootRootDir + "/dev/ptmx")
                && !pathsInChroot.count("/dev/pts"))
            {
                if (mount("none", (chrootRootDir + "/dev/pts").c_str(), "devpts", 0, "newinstance,mode=0620") == 0)
                {
                    createSymlink("/dev/pts/ptmx", chrootRootDir + "/dev/ptmx");

                    /* Make sure /dev/pts/ptmx is world-writable.  With some
                       Linux versions, it is created with permissions 0.  */
                    chmodPath(chrootRootDir + "/dev/pts/ptmx", 0666);
                } else {
                    if (errno != EINVAL)
                        throw SysError("mounting /dev/pts");
                    bindPath("/dev/pts", chrootRootDir + "/dev/pts");
                    bindPath("/dev/ptmx", chrootRootDir + "/dev/ptmx");
                }
            }

            /* Make /etc unwritable */
            if (!parsedDrv->useUidRange())
                chmodPath(chrootRootDir + "/etc", 0555);

            /* Unshare this mount namespace. This is necessary because
               pivot_root() below changes the root of the mount
               namespace. This means that the call to setns() in
               addDependency() would hide the host's filesystem,
               making it impossible to bind-mount paths from the host
               Nix store into the sandbox. Therefore, we save the
               pre-pivot_root namespace in
               sandboxMountNamespace. Since we made /nix/store a
               shared subtree above, this allows addDependency() to
               make paths appear in the sandbox. */
            if (unshare(CLONE_NEWNS) == -1)
                throw SysError("unsharing mount namespace");

            /* Unshare the cgroup namespace. This means
               /proc/self/cgroup will show the child's cgroup as '/'
               rather than whatever it is in the parent. */
            if (cgroup && unshare(CLONE_NEWCGROUP) == -1)
                throw SysError("unsharing cgroup namespace");

            /* Do the chroot(). */
            if (chdir(chrootRootDir.c_str()) == -1)
                throw SysError("cannot change directory to '%1%'", chrootRootDir);

            if (mkdir("real-root", 0) == -1)
                throw SysError("cannot create real-root directory");

            if (pivot_root(".", "real-root") == -1)
                throw SysError("cannot pivot old root directory onto '%1%'", (chrootRootDir + "/real-root"));

            if (chroot(".") == -1)
                throw SysError("cannot change root directory to '%1%'", chrootRootDir);

            if (umount2("real-root", MNT_DETACH) == -1)
                throw SysError("cannot unmount real root filesystem");

            if (rmdir("real-root") == -1)
                throw SysError("cannot remove real-root directory");

            /* Switch to the sandbox uid/gid in the user namespace,
               which corresponds to the build user or calling user in
               the parent namespace. */
            if (setgid(sandboxGid()) == -1)
                throw SysError("setgid failed");
            if (setuid(sandboxUid()) == -1)
                throw SysError("setuid failed");

            setUser = false;
        }
#endif

        if (chdir(tmpDirInSandbox.c_str()) == -1)
            throw SysError("changing into '%1%'", tmpDir);

        /* Close all other file descriptors. */
        closeMostFDs({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO});

        setPersonality(drv->platform);

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        if (settings.enableCoreDumps) {
            limit.rlim_cur = RLIM_INFINITY;
        }
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* Fill in the environment. */
        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && buildUser) {
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            auto gids = buildUser->getSupplementaryGIDs();
            if (setgroups(gids.size(), gids.data()) == -1)
                throw SysError("cannot set supplementary groups of build user");

            if (setgid(buildUser->getGID()) == -1 ||
                getgid() != buildUser->getGID() ||
                getegid() != buildUser->getGID())
                throw SysError("setgid failed");

            if (setuid(buildUser->getUID()) == -1 ||
                getuid() != buildUser->getUID() ||
                geteuid() != buildUser->getUID())
                throw SysError("setuid failed");
        }

        /* Fill in the arguments. */
        Strings args;

#if __APPLE__
        /* This has to appear before import statements. */
        std::string sandboxProfile = "(version 1)\n";

        if (useChroot) {

            /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
            PathSet ancestry;

            /* We build the ancestry before adding all inputPaths to the store because we know they'll
               all have the same parents (the store), and there might be lots of inputs. This isn't
               particularly efficient... I doubt it'll be a bottleneck in practice */
            for (auto & i : pathsInChroot) {
                Path cur = i.first;
                while (cur.compare("/") != 0) {
                    cur = dirOf(cur);
                    ancestry.insert(cur);
                }
            }

            /* And we want the store in there regardless of how empty pathsInChroot. We include the innermost
               path component this time, since it's typically /nix/store and we care about that. */
            Path cur = worker.store.storeDir;
            while (cur.compare("/") != 0) {
                ancestry.insert(cur);
                cur = dirOf(cur);
            }

            /* Add all our input paths to the chroot */
            for (auto & i : inputPaths) {
                auto p = worker.store.printStorePath(i);
                pathsInChroot[p] = p;
            }

            /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
            if (settings.darwinLogSandboxViolations) {
                sandboxProfile += "(deny default)\n";
            } else {
                sandboxProfile += "(deny default (with no-log))\n";
            }

            sandboxProfile +=
                #include "sandbox-defaults.sb"
                ;

            if (!derivationType->isSandboxed())
                sandboxProfile +=
                    #include "sandbox-network.sb"
                    ;

            /* Add the output paths we'll use at build-time to the chroot */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & [_, path] : scratchOutputs)
                sandboxProfile += fmt("\t(subpath \"%s\")\n", worker.store.printStorePath(path));

            sandboxProfile += ")\n";

            /* Our inputs (transitive dependencies and any impurities computed above)

               without file-write* allowed, access() incorrectly returns EPERM
             */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & i : pathsInChroot) {
                if (i.first != i.second.source)
                    throw Error(
                        "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin",
                        i.first, i.second.source);

                std::string path = i.first;
                struct stat st;
                if (lstat(path.c_str(), &st)) {
                    if (i.second.optional && errno == ENOENT)
                        continue;
                    throw SysError("getting attributes of path '%s", path);
                }
                if (S_ISDIR(st.st_mode))
                    sandboxProfile += fmt("\t(subpath \"%s\")\n", path);
                else
                    sandboxProfile += fmt("\t(literal \"%s\")\n", path);
            }
            sandboxProfile += ")\n";

            /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
            sandboxProfile += "(allow file-read*\n";
            for (auto & i : ancestry) {
                sandboxProfile += fmt("\t(literal \"%s\")\n", i);
            }
            sandboxProfile += ")\n";

            sandboxProfile += additionalSandboxProfile;
        } else
            sandboxProfile +=
                #include "sandbox-minimal.sb"
                ;

        debug("Generated sandbox profile:");
        debug(sandboxProfile);

        bool allowLocalNetworking = parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

        /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
           to find temporary directories, so we want to open up a broader place for them to put their files, if needed. */
        Path globalTmpDir = canonPath(defaultTempDir(), true);

        /* They don't like trailing slashes on subpath directives */
        if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

        if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
            Strings sandboxArgs;
            sandboxArgs.push_back("_GLOBAL_TMP_DIR");
            sandboxArgs.push_back(globalTmpDir);
            if (allowLocalNetworking) {
                sandboxArgs.push_back("_ALLOW_LOCAL_NETWORKING");
                sandboxArgs.push_back("1");
            }
            if (sandbox_init_with_parameters(sandboxProfile.c_str(), 0, stringsToCharPtrs(sandboxArgs).data(), nullptr)) {
                writeFull(STDERR_FILENO, "failed to configure sandbox\n");
                _exit(1);
            }
        }
#endif

        args.push_back(std::string(baseNameOf(drv->builder)));

        for (auto & i : drv->args)
            args.push_back(rewriteStrings(i, inputRewrites));

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, std::string("\2\n"));

        sendException = false;

        /* Execute the program.  This should not return. */
        if (drv->isBuiltin()) {
            try {
                logger = makeJSONLogger(*logger);

                BasicDerivation & drv2(*drv);
                for (auto & e : drv2.env)
                    e.second = rewriteStrings(e.second, inputRewrites);

                if (drv->builder == "builtin:fetchurl")
                    builtinFetchurl(drv2, netrcData);
                else if (drv->builder == "builtin:buildenv")
                    builtinBuildenv(drv2);
                else if (drv->builder == "builtin:unpack-channel")
                    builtinUnpackChannel(drv2);
                else
                    throw Error("unsupported builtin builder '%1%'", drv->builder.substr(8));
                _exit(0);
            } catch (std::exception & e) {
                writeFull(STDERR_FILENO, e.what() + std::string("\n"));
                _exit(1);
            }
        }

        execBuilder(drv->builder, args, envStrs);
        // execBuilder should not return

        throw SysError("executing '%1%'", drv->builder);

    } catch (Error & e) {
        if (sendException) {
            writeFull(STDERR_FILENO, "\1\n");
            FdSink sink(STDERR_FILENO);
            sink << e;
            sink.flush();
        } else
            std::cerr << e.msg();
        _exit(1);
    }
}

void LocalDerivationGoal::execBuilder(std::string builder, Strings args, Strings envStrs)
{
    execve(builder.c_str(), stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
}


SingleDrvOutputs LocalDerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here.

       We can only early return when the outputs are known a priori. For
       floating content-addressed derivations this isn't the case.
     */
    if (hook)
        return DerivationGoal::registerOutputs();

    std::map<std::string, ValidPathInfo> infos;

    /* Set of inodes seen during calls to canonicalisePathMetaData()
       for this build's outputs.  This needs to be shared between
       outputs to allow hard links between outputs. */
    InodesSeen inodesSeen;

    Path checkSuffix = ".check";

    std::exception_ptr delayedException;

    /* The paths that can be referenced are the input closures, the
       output paths, and any paths that have been built via recursive
       Nix calls. */
    StorePathSet referenceablePaths;
    for (auto & p : inputPaths) referenceablePaths.insert(p);
    for (auto & i : scratchOutputs) referenceablePaths.insert(i.second);
    for (auto & p : addedPaths) referenceablePaths.insert(p);

    /* FIXME `needsHashRewrite` should probably be removed and we get to the
       real reason why we aren't using the chroot dir */
    auto toRealPathChroot = [&](const Path & p) -> Path {
        return useChroot && !needsHashRewrite()
            ? chrootRootDir + p
            : worker.store.toRealPath(p);
    };

    /* Check whether the output paths were created, and make all
       output paths read-only.  Then get the references of each output (that we
       might need to register), so we can topologically sort them. For the ones
       that are most definitely already installed, we just store their final
       name so we can also use it in rewrites. */
    StringSet outputsToSort;
    struct AlreadyRegistered { StorePath path; };
    struct PerhapsNeedToRegister { StorePathSet refs; };
    std::map<std::string, std::variant<AlreadyRegistered, PerhapsNeedToRegister>> outputReferencesIfUnregistered;
    std::map<std::string, struct stat> outputStats;
    for (auto & [outputName, _] : drv->outputs) {
        auto scratchOutput = get(scratchOutputs, outputName);
        if (!scratchOutput)
            throw BuildError(
                "builder for '%s' has no scratch output for '%s'",
                worker.store.printStorePath(drvPath), outputName);
        auto actualPath = toRealPathChroot(worker.store.printStorePath(*scratchOutput));

        outputsToSort.insert(outputName);

        /* Updated wanted info to remove the outputs we definitely don't need to register */
        auto initialOutput = get(initialOutputs, outputName);
        if (!initialOutput)
            throw BuildError(
                "builder for '%s' has no initial output for '%s'",
                worker.store.printStorePath(drvPath), outputName);
        auto & initialInfo = *initialOutput;

        /* Don't register if already valid, and not checking */
        initialInfo.wanted = buildMode == bmCheck
            || !(initialInfo.known && initialInfo.known->isValid());
        if (!initialInfo.wanted) {
            outputReferencesIfUnregistered.insert_or_assign(
                outputName,
                AlreadyRegistered { .path = initialInfo.known->path });
            continue;
        }

        auto optSt = maybeLstat(actualPath.c_str());
        if (!optSt)
            throw BuildError(
                "builder for '%s' failed to produce output path for output '%s' at '%s'",
                worker.store.printStorePath(drvPath), outputName, actualPath);
        struct stat & st = *optSt;

#ifndef __CYGWIN__
        /* Check that the output is not group or world writable, as
           that means that someone else can have interfered with the
           build.  Also, the output should be owned by the build
           user. */
        if ((!S_ISLNK(st.st_mode) && (st.st_mode & (S_IWGRP | S_IWOTH))) ||
            (buildUser && st.st_uid != buildUser->getUID()))
            throw BuildError(
                    "suspicious ownership or permission on '%s' for output '%s'; rejecting this build output",
                    actualPath, outputName);
#endif

        /* Canonicalise first.  This ensures that the path we're
           rewriting doesn't contain a hard link to /etc/shadow or
           something like that. */
        canonicalisePathMetaData(
            actualPath,
            buildUser ? std::optional(buildUser->getUIDRange()) : std::nullopt,
            inodesSeen);

        bool discardReferences = false;
        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            if (auto udr = get(*structuredAttrs, "unsafeDiscardReferences")) {
                if (auto output = get(*udr, outputName)) {
                    if (!output->is_boolean())
                        throw Error("attribute 'unsafeDiscardReferences.\"%s\"' of derivation '%s' must be a Boolean", outputName, drvPath.to_string());
                    discardReferences = output->get<bool>();
                }
            }
        }

        StorePathSet references;
        if (discardReferences)
            debug("discarding references of output '%s'", outputName);
        else {
            debug("scanning for references for output '%s' in temp location '%s'", outputName, actualPath);

            /* Pass blank Sink as we are not ready to hash data at this stage. */
            NullSink blank;
            references = scanForReferences(blank, actualPath, referenceablePaths);
        }

        outputReferencesIfUnregistered.insert_or_assign(
            outputName,
            PerhapsNeedToRegister { .refs = references });
        outputStats.insert_or_assign(outputName, std::move(st));
    }

    auto sortedOutputNames = topoSort(outputsToSort,
        {[&](const std::string & name) {
            auto orifu = get(outputReferencesIfUnregistered, name);
            if (!orifu)
                throw BuildError(
                    "no output reference for '%s' in build of '%s'",
                    name, worker.store.printStorePath(drvPath));
            return std::visit(overloaded {
                /* Since we'll use the already installed versions of these, we
                   can treat them as leaves and ignore any references they
                   have. */
                [&](const AlreadyRegistered &) { return StringSet {}; },
                [&](const PerhapsNeedToRegister & refs) {
                    StringSet referencedOutputs;
                    /* FIXME build inverted map up front so no quadratic waste here */
                    for (auto & r : refs.refs)
                        for (auto & [o, p] : scratchOutputs)
                            if (r == p)
                                referencedOutputs.insert(o);
                    return referencedOutputs;
                },
            }, *orifu);
        }},
        {[&](const std::string & path, const std::string & parent) {
            // TODO with more -vvvv also show the temporary paths for manual inspection.
            return BuildError(
                "cycle detected in build of '%s' in the references of output '%s' from output '%s'",
                worker.store.printStorePath(drvPath), path, parent);
        }});

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    OutputPathMap finalOutputs;

    for (auto & outputName : sortedOutputNames) {
        auto output = get(drv->outputs, outputName);
        auto scratchPath = get(scratchOutputs, outputName);
        assert(output && scratchPath);
        auto actualPath = toRealPathChroot(worker.store.printStorePath(*scratchPath));

        auto finish = [&](StorePath finalStorePath) {
            /* Store the final path */
            finalOutputs.insert_or_assign(outputName, finalStorePath);
            /* The rewrite rule will be used in downstream outputs that refer to
               use. This is why the topological sort is essential to do first
               before this for loop. */
            if (*scratchPath != finalStorePath)
                outputRewrites[std::string { scratchPath->hashPart() }] = std::string { finalStorePath.hashPart() };
        };

        auto orifu = get(outputReferencesIfUnregistered, outputName);
        assert(orifu);

        std::optional<StorePathSet> referencesOpt = std::visit(overloaded {
            [&](const AlreadyRegistered & skippedFinalPath) -> std::optional<StorePathSet> {
                finish(skippedFinalPath.path);
                return std::nullopt;
            },
            [&](const PerhapsNeedToRegister & r) -> std::optional<StorePathSet> {
                return r.refs;
            },
        }, *orifu);

        if (!referencesOpt)
            continue;
        auto references = *referencesOpt;

        auto rewriteOutput = [&](const StringMap & rewrites) {
            /* Apply hash rewriting if necessary. */
            if (!rewrites.empty()) {
                debug("rewriting hashes in '%1%'; cross fingers", actualPath);

                GeneratorSource dump{dumpPath(actualPath)};
                RewritingSource rewritten(rewrites, dump);
                Path tmpPath = actualPath + ".tmp";
                restorePath(tmpPath, rewritten);
                deletePath(actualPath);
                movePath(tmpPath, actualPath);

                /* FIXME: set proper permissions in restorePath() so
                   we don't have to do another traversal. */
                canonicalisePathMetaData(actualPath, {}, inodesSeen);
            }
        };

        auto rewriteRefs = [&]() -> StoreReferences {
            /* In the CA case, we need the rewritten refs to calculate the
               final path, therefore we look for a *non-rewritten
               self-reference, and use a bool rather try to solve the
               computationally intractable fixed point. */
            StoreReferences res {
                .self = false,
            };
            for (auto & r : references) {
                auto name = r.name();
                auto origHash = std::string { r.hashPart() };
                if (r == *scratchPath) {
                    res.self = true;
                } else if (auto outputRewrite = get(outputRewrites, origHash)) {
                    std::string newRef = *outputRewrite;
                    newRef += '-';
                    newRef += name;
                    res.others.insert(StorePath { newRef });
                } else {
                    res.others.insert(r);
                }
            }
            return res;
        };

        auto newInfoFromCA = [&](const DerivationOutput::CAFloating outputHash) -> ValidPathInfo {
            auto st = get(outputStats, outputName);
            if (!st)
                throw BuildError(
                    "output path %1% without valid stats info",
                    actualPath);
            if (outputHash.method == ContentAddressMethod { FileIngestionMethod::Flat } ||
                outputHash.method == ContentAddressMethod { TextIngestionMethod {} })
            {
                /* The output path should be a regular file without execute permission. */
                if (!S_ISREG(st->st_mode) || (st->st_mode & S_IXUSR) != 0)
                    throw BuildError(
                        "output path '%1%' should be a non-executable regular file "
                        "since recursive hashing is not enabled (one of outputHashMode={flat,text} is true)",
                        actualPath);
            }
            rewriteOutput(outputRewrites);
            /* FIXME optimize and deduplicate with addToStore */
            std::string oldHashPart { scratchPath->hashPart() };
            auto input = std::visit(overloaded {
                [&](const TextIngestionMethod &) -> GeneratorSource {
                    return GeneratorSource(readFileSource(actualPath));
                },
                [&](const FileIngestionMethod & m2) -> GeneratorSource {
                    switch (m2) {
                    case FileIngestionMethod::Recursive:
                        return GeneratorSource(dumpPath(actualPath));
                    case FileIngestionMethod::Flat:
                        return GeneratorSource(readFileSource(actualPath));
                    }
                    assert(false);
                },
            }, outputHash.method.raw);
            auto got = computeHashModulo(outputHash.hashType, oldHashPart, input).first;

            auto optCA = ContentAddressWithReferences::fromPartsOpt(
                outputHash.method,
                std::move(got),
                rewriteRefs());
            if (!optCA) {
                // TODO track distinct failure modes separately (at the time of
                // writing there is just one but `nullopt` is unclear) so this
                // message can't get out of sync.
                throw BuildError("output path '%s' has illegal content address, probably a spurious self-reference with text hashing");
            }
            ValidPathInfo newInfo0 {
                worker.store,
                outputPathName(drv->name, outputName),
                std::move(*optCA),
                Hash::dummy,
            };
            if (*scratchPath != newInfo0.path) {
                // If the path has some self-references, we need to rewrite
                // them.
                // (note that this doesn't invalidate the ca hash we calculated
                // above because it's computed *modulo the self-references*, so
                // it already takes this rewrite into account).
                rewriteOutput(
                    StringMap{{oldHashPart,
                               std::string(newInfo0.path.hashPart())}});
            }

            HashResult narHashAndSize = hashPath(htSHA256, actualPath);
            newInfo0.narHash = narHashAndSize.first;
            newInfo0.narSize = narHashAndSize.second;

            assert(newInfo0.ca);
            return newInfo0;
        };

        ValidPathInfo newInfo = std::visit(overloaded {

            [&](const DerivationOutput::InputAddressed & output) {
                /* input-addressed case */
                auto requiredFinalPath = output.path;
                /* Preemptively add rewrite rule for final hash, as that is
                   what the NAR hash will use rather than normalized-self references */
                if (*scratchPath != requiredFinalPath)
                    outputRewrites.insert_or_assign(
                        std::string { scratchPath->hashPart() },
                        std::string { requiredFinalPath.hashPart() });
                rewriteOutput(outputRewrites);
                auto narHashAndSize = hashPath(htSHA256, actualPath);
                ValidPathInfo newInfo0 { requiredFinalPath, narHashAndSize.first };
                newInfo0.narSize = narHashAndSize.second;
                auto refs = rewriteRefs();
                newInfo0.references = std::move(refs.others);
                if (refs.self)
                    newInfo0.references.insert(newInfo0.path);
                return newInfo0;
            },

            [&](const DerivationOutput::CAFixed & dof) {
                auto & wanted = dof.ca.hash;

                // Replace the output by a fresh copy of itself to make sure
                // that there's no stale file descriptor pointing to it
                Path tmpOutput = actualPath + ".tmp";
                movePath(actualPath, tmpOutput);
                copyFile(tmpOutput, actualPath, { .deleteAfter = true });

                auto newInfo0 = newInfoFromCA(DerivationOutput::CAFloating {
                    .method = dof.ca.method,
                    .hashType = wanted.type,
                });

                /* Check wanted hash */
                assert(newInfo0.ca);
                auto & got = newInfo0.ca->hash;
                if (wanted != got) {
                    /* Throw an error after registering the path as
                       valid. */
                    worker.hashMismatch = true;
                    // XXX: shameless layering violation hack that makes the hash mismatch error at least not utterly worthless
                    auto guessedUrl = getOr(drv->env, "urls", getOr(drv->env, "url", "(unknown)"));
                    delayedException = std::make_exception_ptr(
                        BuildError("hash mismatch in fixed-output derivation '%s':\n likely URL: %s\n  specified: %s\n     got:    %s",
                            worker.store.printStorePath(drvPath),
                            guessedUrl,
                            wanted.to_string(SRI, true),
                            got.to_string(SRI, true)));
                }
                if (!newInfo0.references.empty())
                    delayedException = std::make_exception_ptr(
                        BuildError("illegal path references in fixed-output derivation '%s'",
                            worker.store.printStorePath(drvPath)));

                return newInfo0;
            },

            [&](const DerivationOutput::CAFloating & dof) {
                return newInfoFromCA(dof);
            },

            [&](const DerivationOutput::Deferred &) -> ValidPathInfo {
                // No derivation should reach that point without having been
                // rewritten first
                assert(false);
            },

            [&](const DerivationOutput::Impure & doi) {
                return newInfoFromCA(DerivationOutput::CAFloating {
                    .method = doi.method,
                    .hashType = doi.hashType,
                });
            },

        }, output->raw);

        /* FIXME: set proper permissions in restorePath() so
            we don't have to do another traversal. */
        canonicalisePathMetaData(actualPath, {}, inodesSeen);

        /* Calculate where we'll move the output files. In the checking case we
           will leave leave them where they are, for now, rather than move to
           their usual "final destination" */
        auto finalDestPath = worker.store.printStorePath(newInfo.path);

        /* Lock final output path, if not already locked. This happens with
           floating CA derivations and hash-mismatching fixed-output
           derivations. */
        PathLocks dynamicOutputLock;
        dynamicOutputLock.setDeletion(true);
        auto optFixedPath = output->path(worker.store, drv->name, outputName);
        if (!optFixedPath ||
            worker.store.printStorePath(*optFixedPath) != finalDestPath)
        {
            assert(newInfo.ca);
            dynamicOutputLock.lockPaths({worker.store.toRealPath(finalDestPath)});
        }

        /* Move files, if needed */
        if (worker.store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(worker.store.toRealPath(finalDestPath), actualPath);
                actualPath = worker.store.toRealPath(finalDestPath);
            } else if (buildMode == bmCheck) {
                /* Path already exists, and we want to compare, so we leave out
                   new path in place. */
            } else if (worker.store.isValidPath(newInfo.path)) {
                /* Path already exists because CA path produced by something
                   else. No moving needed. */
                assert(newInfo.ca);
            } else {
                auto destPath = worker.store.toRealPath(finalDestPath);
                deletePath(destPath);
                movePath(actualPath, destPath);
                actualPath = destPath;
            }
        }

        auto & localStore = getLocalStore();

        if (buildMode == bmCheck) {

            if (!worker.store.isValidPath(newInfo.path)) continue;
            ValidPathInfo oldInfo(*worker.store.queryPathInfo(newInfo.path));
            if (newInfo.narHash != oldInfo.narHash) {
                worker.checkMismatch = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    auto dst = worker.store.toRealPath(finalDestPath + checkSuffix);
                    deletePath(dst);
                    movePath(actualPath, dst);

                    handleDiffHook(
                        buildUser ? buildUser->getUID() : getuid(),
                        buildUser ? buildUser->getGID() : getgid(),
                        finalDestPath, dst, worker.store.printStorePath(drvPath), tmpDir);

                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs from '%s'",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath), dst);
                } else
                    throw NotDeterministic("derivation '%s' may not be deterministic: output '%s' differs",
                        worker.store.printStorePath(drvPath), worker.store.toRealPath(finalDestPath));
            }

            /* Since we verified the build, it's now ultimately trusted. */
            if (!oldInfo.ultimate) {
                oldInfo.ultimate = true;
                localStore.signPathInfo(oldInfo);
                localStore.registerValidPaths({{oldInfo.path, oldInfo}});
            }

            continue;
        }

        /* For debugging, print out the referenced and unreferenced paths. */
        for (auto & i : inputPaths) {
            if (references.count(i))
                debug("referenced input: '%1%'", worker.store.printStorePath(i));
            else
                debug("unreferenced input: '%1%'", worker.store.printStorePath(i));
        }

        localStore.optimisePath(actualPath, NoRepair); // FIXME: combine with scanForReferences()
        worker.markContentsGood(newInfo.path);

        newInfo.deriver = drvPath;
        newInfo.ultimate = true;
        localStore.signPathInfo(newInfo);

        finish(newInfo.path);

        /* If it's a CA path, register it right away. This is necessary if it
           isn't statically known so that we can safely unlock the path before
           the next iteration */
        if (newInfo.ca)
            localStore.registerValidPaths({{newInfo.path, newInfo}});

        infos.emplace(outputName, std::move(newInfo));
    }

    if (buildMode == bmCheck) {
        /* In case of fixed-output derivations, if there are
           mismatches on `--check` an error must be thrown as this is
           also a source for non-determinism. */
        if (delayedException)
            std::rethrow_exception(delayedException);
        return assertPathValidity();
    }

    /* Apply output checks. */
    checkOutputs(infos);

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        auto & localStore = getLocalStore();

        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        localStore.registerValidPaths(infos2);
    }

    /* In case of a fixed-output derivation hash mismatch, throw an
       exception now that we have registered the output as valid. */
    if (delayedException)
        std::rethrow_exception(delayedException);

    /* If we made it this far, we are sure the output matches the derivation
       (since the delayedException would be a fixed output CA mismatch). That
       means it's safe to link the derivation to the output hash. We must do
       that for floating CA derivations, which otherwise couldn't be cached,
       but it's fine to do in all cases. */
    SingleDrvOutputs builtOutputs;

    for (auto & [outputName, newInfo] : infos) {
        auto oldinfo = get(initialOutputs, outputName);
        assert(oldinfo);
        auto thisRealisation = Realisation {
            .id = DrvOutput {
                oldinfo->outputHash,
                outputName
            },
            .outPath = newInfo.path
        };
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
            && drv->type().isPure())
        {
            signRealisation(thisRealisation);
            worker.store.registerDrvOutput(thisRealisation);
        }
        builtOutputs.emplace(outputName, thisRealisation);
    }

    return builtOutputs;
}

void LocalDerivationGoal::signRealisation(Realisation & realisation)
{
    getLocalStore().signRealisation(realisation);
}


void LocalDerivationGoal::checkOutputs(const std::map<std::string, ValidPathInfo> & outputs)
{
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : outputs)
        outputsByPath.emplace(worker.store.printStorePath(output.second.path), output.second);

    for (auto & output : outputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        auto getClosure = [&](const StorePath & path)
        {
            uint64_t closureSize = 0;
            StorePathSet pathsDone;
            std::queue<StorePath> pathsLeft;
            pathsLeft.push(path);

            while (!pathsLeft.empty()) {
                auto path = pathsLeft.front();
                pathsLeft.pop();
                if (!pathsDone.insert(path).second) continue;

                auto i = outputsByPath.find(worker.store.printStorePath(path));
                if (i != outputsByPath.end()) {
                    closureSize += i->second.narSize;
                    for (auto & ref : i->second.references)
                        pathsLeft.push(ref);
                } else {
                    auto info = worker.store.queryPathInfo(path);
                    closureSize += info->narSize;
                    for (auto & ref : info->references)
                        pathsLeft.push(ref);
                }
            }

            return std::make_pair(std::move(pathsDone), closureSize);
        };

        auto applyChecks = [&](const Checks & checks)
        {
            if (checks.maxSize && info.narSize > *checks.maxSize)
                throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                    worker.store.printStorePath(info.path), info.narSize, *checks.maxSize);

            if (checks.maxClosureSize) {
                uint64_t closureSize = getClosure(info.path).second;
                if (closureSize > *checks.maxClosureSize)
                    throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                        worker.store.printStorePath(info.path), closureSize, *checks.maxClosureSize);
            }

            auto checkRefs = [&](const std::optional<Strings> & value, bool allowed, bool recursive)
            {
                if (!value) return;

                /* Parse a list of reference specifiers.  Each element must
                   either be a store path, or the symbolic name of the output
                   of the derivation (such as `out'). */
                StorePathSet spec;
                for (auto & i : *value) {
                    if (worker.store.isStorePath(i))
                        spec.insert(worker.store.parseStorePath(i));
                    else if (auto output = get(outputs, i))
                        spec.insert(output->path);
                    else
                        throw BuildError("derivation contains an illegal reference specifier '%s'", i);
                }

                auto used = recursive
                    ? getClosure(info.path).first
                    : info.references;

                if (recursive && checks.ignoreSelfRefs)
                    used.erase(info.path);

                StorePathSet badPaths;

                for (auto & i : used)
                    if (allowed) {
                        if (!spec.count(i))
                            badPaths.insert(i);
                    } else {
                        if (spec.count(i))
                            badPaths.insert(i);
                    }

                if (!badPaths.empty()) {
                    std::string badPathsStr;
                    for (auto & i : badPaths) {
                        badPathsStr += "\n  ";
                        badPathsStr += worker.store.printStorePath(i);
                    }
                    throw BuildError("output '%s' is not allowed to refer to the following paths:%s",
                        worker.store.printStorePath(info.path), badPathsStr);
                }
            };

            checkRefs(checks.allowedReferences, true, false);
            checkRefs(checks.allowedRequisites, true, true);
            checkRefs(checks.disallowedReferences, false, false);
            checkRefs(checks.disallowedRequisites, false, true);
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            if (auto outputChecks = get(*structuredAttrs, "outputChecks")) {
                if (auto output = get(*outputChecks, outputName)) {
                    Checks checks;

                    if (auto maxSize = get(*output, "maxSize"))
                        checks.maxSize = maxSize->get<uint64_t>();

                    if (auto maxClosureSize = get(*output, "maxClosureSize"))
                        checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                    auto get_ = [&](const std::string & name) -> std::optional<Strings> {
                        if (auto i = get(*output, name)) {
                            Strings res;
                            for (auto j = i->begin(); j != i->end(); ++j) {
                                if (!j->is_string())
                                    throw Error("attribute '%s' of derivation '%s' must be a list of strings", name, worker.store.printStorePath(drvPath));
                                res.push_back(j->get<std::string>());
                            }
                            checks.disallowedRequisites = res;
                            return res;
                        }
                        return {};
                    };

                    checks.allowedReferences = get_("allowedReferences");
                    checks.allowedRequisites = get_("allowedRequisites");
                    checks.disallowedReferences = get_("disallowedReferences");
                    checks.disallowedRequisites = get_("disallowedRequisites");

                    applyChecks(checks);
                }
            }
        } else {
            // legacy non-structured-attributes case
            Checks checks;
            checks.ignoreSelfRefs = true;
            checks.allowedReferences = parsedDrv->getStringsAttr("allowedReferences");
            checks.allowedRequisites = parsedDrv->getStringsAttr("allowedRequisites");
            checks.disallowedReferences = parsedDrv->getStringsAttr("disallowedReferences");
            checks.disallowedRequisites = parsedDrv->getStringsAttr("disallowedRequisites");
            applyChecks(checks);
        }
    }
}


void LocalDerivationGoal::deleteTmpDir(bool force)
{
    if (tmpDir != "") {
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError("note: keeping build directory '%s'", tmpDir);
            chmod(tmpDir.c_str(), 0755);
        }
        else
            deletePath(tmpDir);
        tmpDir = "";
    }
}


bool LocalDerivationGoal::isReadDesc(int fd)
{
    return (hook && DerivationGoal::isReadDesc(fd)) ||
        (!hook && fd == builderOut.get());
}


StorePath LocalDerivationGoal::makeFallbackPath(OutputNameView outputName)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName),
        Hash(htSHA256), outputPathName(drv->name, outputName));
}


StorePath LocalDerivationGoal::makeFallbackPath(const StorePath & path)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string()),
        Hash(htSHA256), path.name());
}


}
