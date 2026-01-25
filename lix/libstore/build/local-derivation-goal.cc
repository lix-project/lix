#include "lix/libstore/build/local-derivation-goal.hh"
#include "derivation-goal.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libstore/indirect-root-store.hh"
#include "lix/libstore/machines.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/temporary-dir.hh"
#include "lix/libstore/build/worker.hh"
#include "lix/libstore/builtins.hh"
#include "lix/libstore/builtins/buildenv.hh"
#include "lix/libstore/path-references.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/daemon.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/topo-sort.hh"
#include "lix/libutil/json.hh"
#include "lix/libstore/build/personality.hh"
#include "lix/libutil/namespaces.hh"
#include "lix/libstore/build/child.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libutil/mount.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/thread-name.hh"
#include "lix/libstore/platform/linux.hh"
#include "lix/libstore/path-tree.hh"

#include <cstddef>
#include <dirent.h>
#include <exception>
#include <optional>
#include <regex>
#include <queue>

#include <stdexcept>
#include <sys/stat.h>
#include <string>
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
#include <sys/syscall.h>
#endif

#include <pwd.h>
#include <grp.h>
#include <iostream>

namespace nix {

static kj::Promise<Result<void>> handleDiffHook(
    std::optional<uid_t> uid,
    std::optional<uid_t> gid,
    const Path & tryA,
    const Path & tryB,
    const Path & drvPath,
    const Path & tmpDir
)
try {
    auto & diffHookOpt = settings.diffHook.get();
    if (diffHookOpt && settings.runDiffHook) {
        auto & diffHook = *diffHookOpt;
        try {
            auto hook = runHelper(
                "run-diff-hook",
                {
                    .args =
                        {uid ? std::to_string(*uid) : "-",
                         gid ? std::to_string(*gid) : "-",
                         diffHook,
                         tryA,
                         tryB,
                         drvPath,
                         tmpDir},
                    .captureStdout = true,
                }
            );
            auto diffRes = TRY_AWAIT(hook.getStdout()->drain());
            hook.waitAndCheck();
            if (diffRes != "")
                printError("%1%", Uncolored(chomp(diffRes)));
        } catch (Error & error) {
            ErrorInfo ei = error.info();
            // FIXME: wrap errors.
            ei.msg = HintFmt("diff hook execution failed: %s", ei.msg.str());
            logError(ei);
        }
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

const Path LocalDerivationGoal::homeDir = "/homeless-shelter";


LocalDerivationGoal::~LocalDerivationGoal() noexcept(false)
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { killChild(); } catch (...) { ignoreExceptionInDestructor(); }
    try {
        finalizeTmpDir(false, true);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
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
    if (pg) {
        /* If we're using a build user, then there is a tricky race
           condition: if we kill the build user before the child has
           done its setuid() to the build user uid, then it won't be
           killed, and we'll potentially lock up in pid.wait().  So
           also send a conventional kill to the child. */
        pg.kill();

        killSandbox(true);
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

uid_t LocalDerivationGoal::sandboxUid()
{
    return worker.namespaces.user ? (!buildUser || buildUser->getUIDCount() == 1 ? 1000 : 0)
                                  : buildUser->getUID();
}

gid_t LocalDerivationGoal::sandboxGid()
{
    return worker.namespaces.user ? (!buildUser || buildUser->getUIDCount() == 1 ? 100 : 0)
                                  : buildUser->getGID();
}

kj::Promise<Result<Goal::WorkResult>> LocalDerivationGoal::tryLocalBuild() noexcept
try {
retry:
#if __APPLE__
    additionalSandboxProfile = parsedDrv->getStringAttr("__sandboxProfile").value_or("");
#endif

    if (!slotToken.valid()) {
        outputLocks.reset();
        if (worker.localBuilds.capacity() > 0) {
            slotToken = co_await worker.localBuilds.acquire();
            co_return co_await tryToBuild();
        }
        if (getMachines().empty()) {
            throw Error(
                "unable to start any build; either set '--max-jobs' to a non-zero value or enable "
                "remote builds.\n"
                "https://docs.lix.systems/manual/lix/stable/advanced-topics/distributed-builds.html"
            );
        } else {
            throw Error(
                "unable to start any build; remote machines may not have all required system features.\n"
                "https://docs.lix.systems/manual/lix/stable/advanced-topics/distributed-builds.html"
            );
        }
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
    const bool sandboxFallbackAllowed = [&]() {
        if (localStore.config().storeDir != localStore.config().realStoreDir.get()) {
#if __linux__
            if (!useChroot) {
                printTaggedWarning("auto-enabling the sandbox due to using a diverted store");
            }
            useChroot = true;
            return false;
#else
            throw Error("building using a diverted store is not supported on this platform");
#endif
        }
        return true;
    }();

    if (useBuildUsers()) {
        if (!buildUser)
            buildUser = acquireUserLock(parsedDrv->useUidRange() ? 65536 : 1, useChroot);

        if (!buildUser) {
            if (!actLock)
                actLock = logger->startActivity(
                    lvlWarn,
                    actBuildWaiting,
                    fmt("waiting for a free build user ID for '%s'",
                        Magenta(worker.store.printStorePath(drvPath)))
                );
            co_await waitForAWhile();
            // we can loop very often, and `co_return co_await` always allocates a new frame
            goto retry;
        }
    }

    #if __linux__
    if (useChroot) {
        // FIXME: should user namespaces being unsupported also require
        // sandbox-fallback to be allowed? I don't think so, since they aren't a
        // huge security win to have enabled.

        if (!worker.namespaces.mountAndPid) {
            if (!settings.sandboxFallback)
                throw Error("this system does not support the kernel namespaces that are required for sandboxing; use '--no-sandbox' to disable sandboxing. Pass --debug for diagnostics on what is broken.");
            if (!sandboxFallbackAllowed)
                throw Error("Sandboxing is enabled due to using a diverted store, but this system does not support the kernel namespaces that are required. Pass --debug for diagnostics on what is broken.");
            printTaggedWarning("auto-disabling sandboxing because the prerequisite namespaces are not available");
            useChroot = false;
        }

        if (!worker.namespaces.user && !buildUser) {
            throw Error("cannot perform a sandboxed build because user namespaces are not available.\nIn this Lix's configuration, user namespaces are required due to either being non-root, or build-users-group being disabled without also enabling auto-allocate-uids");
        }
    }
#else
    // NOTE: This is needed to silence the `unused-variable` warning on non-linux machines
    (void) sandboxFallbackAllowed;
#endif

    actLock.reset();

    try {

        /* Okay, we have to build. */
        TRY_AWAIT(startBuilder());

        mcRunningBuilds = worker.runningBuilds.addTemporarily(1);
        co_return TRY_AWAIT(wrapChildHandler(handleRawChild()));

    } catch (BuildError & e) {
        outputLocks.reset();
        buildUser.reset();
        auto report = done(BuildResult::InputRejected, {}, std::move(e));
        report.permanentFailure = true;
        co_return report;
    }
} catch (...) {
    co_return result::current_exception();
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
    return hook ? DerivationGoal::getChildStatus() : pg.kill();
}

void LocalDerivationGoal::closeReadPipes()
{
    if (hook) {
        DerivationGoal::closeReadPipes();
    } else {
        builderOutPTY.close();
    }
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
}


void LocalDerivationGoal::cleanupPostChildKill()
{
    /* When running under a build user, make sure that all processes
       running under that uid are gone.  This is to prevent a
       malicious user from leaving behind a process that keeps files
       open and modifies them after they have been chown'ed to
       root. */
    killSandbox(true);
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
        if (sys::statvfs(localStore.config().realStoreDir, &st) == 0
            && (uint64_t) st.f_bavail * st.f_bsize < required)
        {
            diskFull = true;
        }
        if (sys::statvfs(tmpDirRoot, &st) == 0 && (uint64_t) st.f_bavail * st.f_bsize < required) {
            diskFull = true;
        }
    }
#endif

    finalizeTmpDir(false);

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
    finalizeTmpDir(true);
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

kj::Promise<Result<void>> LocalDerivationGoal::startBuilder()
try {
#if !(__linux__)
    if (buildUser && buildUser->getUIDCount() != 1) {
        throw Error("cgroups are not supported on this platform");
    }
#endif

    /* Make sure that no other processes are executing under the
       sandbox uids. This must be done before any chownToBuilder()
       calls. */
    killSandbox(false);

    /* Right platform? */
    if (!parsedDrv->canBuildLocally(worker.store)) {
        HintFmt addendum{""};
        if (settings.useSubstitutes && !parsedDrv->substitutesAllowed()) {
            addendum = HintFmt("\n\nHint: the failing derivation has %s set to %s, forcing it to be built rather than substituted.\n"
                "Passing %s to force substitution may resolve this failure if the path is available in a substituter.",
                "allowSubstitutes", "false", "--always-allow-substitutes");
        }
        throw Error({
            .msg = HintFmt("a '%s' with features {%s} is required to build '%s', but I am a '%s' with features {%s}%s",
                drv->platform,
                concatStringsSep(", ", parsedDrv->getRequiredSystemFeatures()),
                worker.store.printStorePath(drvPath),
                settings.thisSystem,
                concatStringsSep<StringSet>(", ", worker.store.config().systemFeatures),
                Uncolored(addendum))
        });
    }

    try {
        auto buildDir = worker.buildDirOverride.value_or(settings.buildDir.get());

        createDirs(buildDir);

        /* Create a temporary directory where the build will take
           place. */
        tmpDirRoot = createTempSubdir(buildDir, std::nullopt, 0700);
    } catch (SysError & e) {
        /*
         * Fallback to the global tmpdir and create a safe space there
         * only if it's a permission error.
         */
        if (e.errNo != EACCES) {
            throw;
        }

        auto nixBuildsTmp = createTempDir(fmt("nix-builds-%s", geteuid()), 0700);
        printTaggedWarning(
            "Failed to use the system-wide build directory '%s', falling back to a temporary "
            "directory inside '%s'",
            settings.buildDir.get(),
            nixBuildsTmp
        );
        tmpDirRoot = createTempSubdir(nixBuildsTmp, std::nullopt, 0700);
        worker.buildDirOverride = nixBuildsTmp;
    }
    /* The TOCTOU between the previous mkdir call and this open call is unavoidable due to
     * POSIX semantics.*/
    tmpDirRootFd = sys::open(tmpDirRoot, O_RDONLY | O_NOFOLLOW | O_DIRECTORY);
    if (!tmpDirRootFd) {
        throw SysError("failed to open the build temporary directory descriptor '%1%'", tmpDirRoot);
    }

#if __APPLE__
    // The Darwin sandbox ensures that builds cannot change the
    // permissions of their own build directory. Unsandboxed builds
    // disable this, but have no isolation by design anyway. The
    // minimal sandbox (applied even when `sandbox = false`, though not
    // when `_NIX_TEST_NO_SANDBOX` is set) prevents the creation of
    // `set{u,g}id` files regardless.
    tmpDir = tmpDirRoot;
    tmpDirFd = std::move(tmpDirRootFd);
#else
    // place the actual build directory in a subdirectory of tmpDirRoot. if
    // we do not do this a build can `chown 777` its build directory and so
    // make it accessible to everyone in the system, breaking isolation. we
    // also need the intermediate level to be inaccessible to others. build
    // processes must be able to at least traverse to the directory though,
    // without being able to chmod. this means either mode 0750 or 0710. we
    // use 0710 just to be extra safe; if we ever add more directories they
    // will not be enumerable to other processes in the builder user group.
    tmpDir = tmpDirRoot + "/b";
    if (mkdirat(tmpDirRootFd.get(), "b", 0700)) {
        throw SysError("failed to create the build temporary directory '%1%'", tmpDir);
    }
    tmpDirFd = AutoCloseFD{openat(tmpDirRootFd.get(), "b", O_RDONLY | O_NOFOLLOW | O_DIRECTORY)};
    if (!tmpDirFd)
        throw SysError("failed to open the build temporary directory descriptor '%1%'", tmpDir);

    if (buildUser) {
        if (fchown(tmpDirRootFd.get(), -1, buildUser->getGID()) == -1) {
            throw SysError("cannot change ownership of '%1%'", tmpDirRoot);
        }
        if (fchmod(tmpDirRootFd.get(), 0710) == -1) {
            throw SysError("cannot change mode of '%1%'", tmpDirRoot);
        }
    }
#endif

    chownToBuilder(tmpDirFd);

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

    TRY_AWAIT(writeStructuredAttrs());

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
            static std::regex regex = nix::regex::parse("[A-Za-z_][A-Za-z0-9_.-]*");
            if (!std::regex_match(fileName, regex))
                throw Error("invalid file name '%s' in 'exportReferencesGraph'", fileName);

            auto storePathS = *i++;
            if (!worker.store.isInStore(storePathS))
                throw BuildError("'exportReferencesGraph' contains a non-store path '%1%'", storePathS);
            auto storePath = worker.store.toStorePath(storePathS).first;

            /* Write closure info to <fileName>. */
            writeFile(
                tmpDir + "/" + fileName,
                TRY_AWAIT(worker.store.makeValidityRegistration(
                    TRY_AWAIT(worker.store.exportReferences({storePath}, inputPaths)), false, false
                ))
            );
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
        if (worker.store.config().storeDir.starts_with(tmpDirInSandbox))
        {
            throw Error("`sandbox-build-dir` must not contain the storeDir");
        }
        pathsInChroot[tmpDirInSandbox] = tmpDir;

        /* Add the closure of store paths to the chroot. */
        StorePathSet closure;
        for (auto & i : pathsInChroot)
            try {
                if (worker.store.isInStore(i.second.source)) {
                    TRY_AWAIT(worker.store.computeFSClosure(
                        worker.store.toStorePath(i.second.source).first, closure
                    ));
                }
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

    // Note that the derivation may or may not exist when running the
    // pre-build-hook. In the past this was *supposed* to not run the
    // hook in such cases, but at some intermediate point (since this->drv
    // became always an instance of Derivation rather than BasicDerivation), it
    // started being run in all cases on systems using the sandbox.
    // https://git.lix.systems/lix-project/lix/commit/7f5b750b401e98e9e2a346552aba5bd2e0a9203f
    //
    // As such, we just run it every time. It might be reasonable (and more
    // helpful behaviour for users) in the future to write out the derivation
    // to disk if pre-build-hook is in use.
    if (settings.preBuildHook != "") {
        printMsg(lvlChatty, "executing pre-build hook '%1%'", settings.preBuildHook);
        auto drvPathPretty = worker.store.printStorePath(drvPath);
        auto args = useChroot ? Strings({ drvPathPretty, chrootRootDir}) :
            Strings({ drvPathPretty });
        enum BuildHookState {
            stBegin,
            stExtraChrootDirs
        };
        auto state = stBegin;
        std::string lines;
        try {
            TRY_AWAIT(runProgram(settings.preBuildHook, false, args));
        } catch (nix::Error & e) {
            e.addTrace(nullptr,
                "while running pre-build-hook %s for derivation %s",
                settings.preBuildHook,
                drvPathPretty
            );
            throw;
        }
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

    /* Run the builder. */
    printMsg(lvlChatty, "executing builder '%1%'", drv->builder);
    printMsg(lvlChatty, "using builder args '%1%'", concatStringsSep(" ", drv->args));
    for (auto & i : drv->env)
        printMsg(lvlVomit, "setting builder env variable '%1%'='%2%'", i.first, i.second);

    /* Create the log file. */
    openLogFile();

    /* Create a pseudoterminal to get the output of the builder. */
    builderOutPTY = AutoCloseFD{posix_openpt(O_RDWR | O_NOCTTY)};
    if (!builderOutPTY) {
        throw SysError("opening pseudoterminal master");
    }

    // FIXME: not thread-safe, use ptsname_r
    std::string slaveName = ptsname(builderOutPTY.get());

    if (buildUser) {
        if (sys::chmod(slaveName, 0600)) {
            throw SysError("changing mode of pseudoterminal slave");
        }

        // don't chown yet so we can open the pty without DAC override capabilities
    }
#if __APPLE__
    else {
        if (grantpt(builderOutPTY.get()))
            throw SysError("granting access to pseudoterminal slave");
    }
#endif

    if (unlockpt(builderOutPTY.get()))
        throw SysError("unlocking pseudoterminal");

    /* We need to open the slave early, before CLONE_NEWUSER. Otherwise we get
       EPERM when running as root. */
    AutoCloseFD builderOut{sys::open(slaveName, O_RDWR | O_NOCTTY | O_CLOEXEC)};
    if (!builderOut) {
        throw SysError("opening pseudoterminal slave");
    }

    // *now* we chown the pty device node for sandbox processes
    if (buildUser) {
        if (sys::chown(slaveName, buildUser->getUID(), 0)) {
            throw SysError("changing owner of pseudoterminal slave");
        }
    }

    // Put the pt into raw mode to prevent \n -> \r\n translation.
    {
        struct termios term;
        if (tcgetattr(builderOut.get(), &term))
            throw SysError("getting pseudoterminal attributes");

        cfmakeraw(&term);

        if (tcsetattr(builderOut.get(), TCSANOW, &term))
            throw SysError("putting pseudoterminal into raw mode");
    }

    buildResult.startTime = time(0);

    /* Make the contents of netrc and the CA certificate bundle
       available to builtin:fetchurl (which may run under a
       different uid and/or in a sandbox). */
    std::string netrcData;
    std::string caFileData;
    if (drv->isBuiltin() && drv->builder == "builtin:fetchurl" && !derivationType->isSandboxed()) {
        try {
            netrcData = readFile(settings.netrcFile);
        } catch (SysError &) {
        }

        try {
            caFileData = readFile(settings.caFile);
        } catch (SysError &) {
        }
    }

    if (!derivationType->isSandboxed()) {
        setupConfiguredCertificateAuthority();
    }

    /* Fill in the environment. */
    Strings envStrs;
    for (auto & i : env) {
        envStrs.push_back(rewriteStrings(i.first + "=" + i.second, inputRewrites));
    }

    /* Fill in the arguments. */
    Strings args;

    args.push_back(std::string(baseNameOf(drv->builder)));

    for (auto & i : drv->args) {
        args.push_back(rewriteStrings(i, inputRewrites));
    }

    /* Fork a child to build the package. */
    pg = ProcessGroup{startChild(netrcData, caFileData, envStrs, args, std::move(builderOut))};

    /* Check if setting up the build environment failed. */
    std::vector<std::string> msgs;
    while (true) {
        std::string msg = [&]() {
            try {
                return readLine(builderOutPTY.get());
            } catch (Error & e) {
                auto status = pg.wait();
                e.addTrace({}, "while waiting for the build environment for '%s' to initialize (%s, previous messages: %s)",
                    worker.store.printStorePath(drvPath),
                    statusToString(status),
                    concatStringsSep("|", msgs));
                throw;
            }
        }();
        if (msg.substr(0, 1) == "\2") break;
        if (msg.substr(0, 1) == "\1") {
            FdSource source(builderOutPTY.get());
            auto ex = readError(source);
            ex.addTrace({}, "while setting up the build environment");
            throw ex;
        }
        debug("sandbox setup: %1%", Uncolored(msg));
        msgs.push_back(std::move(msg));
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

Pid LocalDerivationGoal::startChild(
    const std::string & netrcData,
    const std::string & caFileData,
    const Strings & envStrs,
    const Strings & args,
    AutoCloseFD logPTY
)
{
    return startProcess([&]() {
        if (dup2(logPTY.get(), STDERR_FILENO) == -1) {
            throw SysError("failed to redirect build output to log file");
        }
        closeOnExec(STDERR_FILENO, false);
        runChild(netrcData, caFileData, envStrs, args);
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
                auto hash = hashString(HashType::SHA256, i.first);
                std::string fn = ".attr-" + hash.to_string(HashFormat::Base32, false);
                Path p = tmpDir + "/" + fn;
                /* TODO(jade): we should have BorrowedFD instead of OwnedFD. */
                AutoCloseFD passAsFileFd{sys::openat(
                    tmpDirFd.get(),
                    fn,
                    O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC | O_EXCL | O_NOFOLLOW,
                    0666
                )};
                if (!passAsFileFd) {
                    throw SysError("opening `passAsFile` file in the sandbox '%1%'", p);
                }
                writeFile(passAsFileFd, rewriteStrings(i.second, inputRewrites));
                chownToBuilder(passAsFileFd);
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

void LocalDerivationGoal::setupConfiguredCertificateAuthority()
{
    if (settings.caFile != "") {
        if (pathAccessible(settings.caFile, true)) {
            auto prefix = useChroot ?
#if __linux__
                                    chrootRootDir
#elif __APPLE__
                                    tmpDir
#else
#error "Your platform has no known behavior under `useChroot` flag"
#endif
                                    : tmpDir;
            debug(
                "rendering visible configured CA '%s' in the builder (prefix directory: '%s')",
                settings.caFile,
                prefix
            );

            // Setting the certificate authorities implies to copy the files inside
            // the builder's environment.
            //
            // Extra care has to be taken to adjust the paths depending on whether
            // we actually have a proper filesystem namespacing or not.
            auto logicalTargetPath = "/etc/ssl/certs/ca-certificates.crt";

            createDirs(prefix + "/etc/ssl/certs");
            copyFile(settings.caFile, prefix + logicalTargetPath, {.followSymlinks = true});

            /* Do not let the derivation dictate what should be these values if `caFile` is
             * set. */
            auto impureVars = parsedDrv->getStringsAttr("impureEnvVars").value_or(Strings());
            if (std::find(impureVars.begin(), impureVars.end(), "NIX_SSL_CERT_FILE") != impureVars.end()
                && env["NIX_SSL_CERT_FILE"] != settings.caFile)
            {
                printTaggedWarning(
                    "'NIX_SSL_CERT_FILE' is an impure environment variable of this "
                    "derivation but a *DIFFERENT* `ssl-cert-file` was set in the settings "
                    "which takes precedence.\n"
                    "If you use `ssl-cert-file`, the certificate gets copied in the builder "
                    "environment and the environment variables are set automatically.\n"
                    "If you set this environment variable to be an impure environment "
                    "variable, you need to ensure it is accessible to the sandbox via "
                    "`extra-sandbox-paths`.\n"
                    "This warning may become a hard error in the future version of Lix."
                );
            }

            /* Currently, outside of Linux, there's no filesystem namespacing. */
            auto certBundleInBuilder =
#if __linux__
                /* If we are using no sandboxing, we still need to use the physical prefix. */
                useChroot ? logicalTargetPath : prefix + logicalTargetPath;
#else
                prefix + logicalTargetPath;
#endif

            env["NIX_SSL_CERT_FILE"] = certBundleInBuilder;
        } else if (pathExists(settings.caFile)) {
            // The path exist but we were not able to access it. This is not a fatal
            // error, warn about this so the user can remediate.
            printTaggedWarning(
                "Configured certificate authority '%1' exists but is inaccessible, it "
                "will not be copied in the sandbox. TLS operations inside the sandbox may "
                "be non-functional.",
                settings.caFile
            );
        }
    }
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
    env["NIX_STORE"] = worker.store.config().storeDir;

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


kj::Promise<Result<void>> LocalDerivationGoal::writeStructuredAttrs()
try {
    if (auto structAttrsJson =
            TRY_AWAIT(parsedDrv->prepareStructuredAttrs(worker.store, inputPaths)))
    {
        auto json = structAttrsJson.value();
        JSON rewritten;
        for (auto & [i, v] : json["outputs"].get<JSON::object_t>()) {
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
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

void LocalDerivationGoal::chownToBuilder(const Path & path)
{
    if (!buildUser) return;
    if (sys::chown(path, buildUser->getUID(), buildUser->getGID()) == -1) {
        throw SysError("cannot change ownership of '%1%'", path);
    }
}

void LocalDerivationGoal::chownToBuilder(const AutoCloseFD & fd)
{
    if (!buildUser) return;
    if (fchown(fd.get(), buildUser->getUID(), buildUser->getGID()) == -1)
        throw SysError("cannot change ownership of file '%1%'", fd.guessOrInventPath());
}

void LocalDerivationGoal::runChild(
    const std::string & netrcData,
    const std::string & caFileData,
    const Strings & envStrs,
    const Strings & args
)
{
    /* Warning: in the child we should absolutely not make any SQLite
       calls! */

    bool sendException = true;

    try { /* child */

        commonExecveingChildInit();

        const bool setUser = prepareChildSetup();

        if (sys::chdir(tmpDirInSandbox) == -1) {
            throw SysError("changing into '%1%'", tmpDir);
        }

        /* Close all other file descriptors. */
        closeExtraFDs();

        /* Disable core dumps by default. */
        struct rlimit limit = { 0, RLIM_INFINITY };
        if (settings.enableCoreDumps) {
            limit.rlim_cur = RLIM_INFINITY;
        }
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

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

        finishChildSetup();

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

                auto getAttr = [&](const std::string & name) {
                    auto i = drv2.env.find(name);
                    if (i == drv2.env.end()) {
                        throw Error("attribute '%s' missing", name);
                    }
                    return i->second;
                };

                if (drv->builder == "builtin:fetchurl") {
                    const auto hash = getAttr("outputHashMode") == "flat" ? [&] -> std::optional<Hash> {
                        const auto ht = parseHashTypeOpt(getAttr("outputHashAlgo"));
                        return newHashAllowEmpty(getAttr("outputHash"), ht);
                    }()
                        : std::nullopt;
                    BuiltinFetchurl{
                        .storePath = getAttr("out"),
                        .mainUrl = getAttr("url"),
                        .unpack = getOr(drv2.env, "unpack", "") == "1",
                        .executable = getOr(drv2.env, "executable", "") == "1",
                        .hash = hash,
                        .netrcData = netrcData,
                        .caFileData = caFileData,
                    }
                        .run();
                } else if (drv->builder == "builtin:buildenv") {
                    builtinBuildenv(
                        getAttr("out"), tokenizeString<Strings>(getAttr("derivations")), getAttr("manifest")
                    );
                } else if (drv->builder == "builtin:unpack-channel") {
                    builtinUnpackChannel(getAttr("out"), getAttr("channelName"), getAttr("src"));
                } else {
                    throw Error("unsupported builtin builder '%1%'", drv->builder.substr(8));
                }
                _exit(0);
            } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
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
    sys::execve(builder, args, envStrs);
}


kj::Promise<Result<SingleDrvOutputs>> LocalDerivationGoal::registerOutputs()
try {
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here.

       We can only early return when the outputs are known a priori. For
       floating content-addressed derivations this isn't the case.
     */
    if (hook)
        co_return TRY_AWAIT(DerivationGoal::registerOutputs());

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

        auto optSt = maybeLstat(actualPath);
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

    std::map<StorePath, StorePathSet> outputGraph;
    std::map<StorePath, std::string> inverseOutputMap;
    for (auto & name : outputsToSort) {
        inverseOutputMap[scratchOutputs.at(name)] = name;
    }

    for (auto & name : outputsToSort) {
        auto orifu = get(outputReferencesIfUnregistered, name);
        if (!orifu) {
            throw BuildError(
                "no output reference for '%s' in build of '%s'",
                name,
                worker.store.printStorePath(drvPath)
            );
        }

        outputGraph[scratchOutputs.at(name)] = StorePathSet{};
        std::visit(
            overloaded{/* Since we'll use the already installed versions of these, we
                                   can treat them as leaves and ignore any references they
                                   have. */
                       [&](const AlreadyRegistered &) {},
                       [&](const PerhapsNeedToRegister & refs) {
                           for (auto & ref : refs.refs) {
                               if (inverseOutputMap.find(ref) != inverseOutputMap.end()) {
                                   outputGraph[scratchOutputs.at(name)].insert(ref);
                               }
                           }
                       }
            },
            *orifu
        );
    }

    auto topoSortedOutputs =
        topoSort(outputsToSort, {[&](const std::string & name) {
                     StringSet dependencies;
                     for (auto & path : outputGraph.at(scratchOutputs.at(name))) {
                         auto outputName = inverseOutputMap.at(path);
                         dependencies.insert(outputName);
                     }
                     return dependencies;
                 }});

    auto & localStore = getLocalStore();
    auto sortedOutputNames = TRY_AWAIT(std::visit(
        overloaded{
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            [&](Cycle<std::string> & cycle) -> kj::Promise<Result<std::vector<std::string>>> {
                try {
                    auto chrootAwareAccessor = getChrootDirAwareFSAccessor();
                    auto graphStr = TRY_AWAIT(genGraphString(
                        scratchOutputs.at(cycle.path),
                        scratchOutputs.at(cycle.parent),
                        outputGraph,
                        worker.store,
                        true,
                        // We need to access store-paths that aren't registered yet for
                        // precise=true. Hence, only do this if a chroot-aware accessor is
                        // implemented in for this platform.
                        chrootAwareAccessor.has_value(),
                        chrootAwareAccessor
                    ));

                    throw BuildError(
                        "cycle detected in build of '%s' in the references of output '%s' from "
                        "output "
                        "'%s'.\n\nShown below are the files inside the outputs leading to the "
                        "cycle:\n%s",
                        worker.store.printStorePath(drvPath),
                        cycle.path,
                        cycle.parent,
                        Uncolored(graphStr)
                    );
                } catch (...) {
                    co_return result::current_exception();
                }
            },
            [](auto & r) -> kj::Promise<Result<std::vector<std::string>>> { co_return r; }
        },
        topoSortedOutputs
    ));

    std::reverse(sortedOutputNames.begin(), sortedOutputNames.end());

    OutputPathMap finalOutputs;

    std::vector<std::pair<Path, std::optional<Path>>> nondeterministic;
    std::map<std::string, StorePath> alreadyRegisteredOutputs;

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
                alreadyRegisteredOutputs.insert_or_assign(outputName, skippedFinalPath.path);
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

        auto newInfoFromCA = [&](ContentAddressMethod method, HashType hashType) -> ValidPathInfo {
            auto st = get(outputStats, outputName);
            if (!st)
                throw BuildError(
                    "output path %1% without valid stats info",
                    actualPath);
            if (method == ContentAddressMethod { FileIngestionMethod::Flat } ||
                method == ContentAddressMethod { TextIngestionMethod {} })
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
            }, method.raw);
            auto got = computeHashModulo(hashType, oldHashPart, input).first;

            auto optCA = ContentAddressWithReferences::fromPartsOpt(
                method,
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

            HashResult narHashAndSize = hashPath(HashType::SHA256, actualPath);
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
                auto narHashAndSize = hashPath(HashType::SHA256, actualPath);
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

                auto newInfo0 = newInfoFromCA(dof.ca.method, wanted.type);

                /* Check wanted hash */
                assert(newInfo0.ca);
                auto & got = newInfo0.ca->hash;
                if (wanted != got) {
                    /* Throw an error after registering the path as
                       valid. */
                    anyHashMismatchSeen = true;
                    // XXX: shameless layering violation hack that makes the hash mismatch error at least not utterly worthless
                    auto guessedUrl = getOr(drv->env, "urls", getOr(drv->env, "url", "(unknown)"));
                    delayedException = std::make_exception_ptr(BuildError(
                        "hash mismatch in fixed-output derivation '%s':\n    likely URL: %s\n     "
                        "specified: %s\n           got: %s\n expected path: %s\n      got path: %s",
                        worker.store.printStorePath(drvPath),
                        guessedUrl,
                        wanted.to_string(),
                        got.to_string(),
                        worker.store.printStorePath(dof.path(worker.store, drv->name, outputName)),
                        worker.store.printStorePath(newInfo0.path)
                    ));
                }
                if (!newInfo0.references.empty()) {
                    std::string references;

                    for (StorePath r : newInfo0.references) {
                        references.append("\n  " + worker.store.printStorePath(r));
                    }

                    delayedException = std::make_exception_ptr(BuildError(
                        "the fixed-output derivation '%s' must not reference store paths but "
                        "%d such references were found:%s",
                        worker.store.printStorePath(drvPath),
                        newInfo0.references.size(),
                        references
                    ));
                }

                return newInfo0;
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
        std::optional<PathLock> dynamicOutputLock;
        auto fixedPath = output->path(worker.store, drv->name, outputName);
        if (worker.store.printStorePath(fixedPath) != finalDestPath) {
            assert(newInfo.ca);
            dynamicOutputLock = TRY_AWAIT(lockPathAsync(worker.store.toRealPath(finalDestPath)));
        }

        /* Move files, if needed */
        if (worker.store.toRealPath(finalDestPath) != actualPath) {
            if (buildMode == bmRepair) {
                /* Path already exists, need to replace it */
                replaceValidPath(worker.store.toRealPath(finalDestPath), actualPath);
                actualPath = worker.store.toRealPath(finalDestPath);
            } else if (buildMode == bmCheck && TRY_AWAIT(worker.store.isValidPath(newInfo.path))) {
                /* Path already exists, and we want to compare, so we
                   don't replace the previously existing output with
                   the new one. */
            } else if (TRY_AWAIT(worker.store.isValidPath(newInfo.path))) {
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

        // Check determinism and run the diff hook for input-addressed
        // paths if we're in check mode.
        // TODO: implement this for content-addressed paths too.
        if (buildMode == bmCheck && !newInfo.ca) {

            // We can only do this if we have a previous output path to compare.
            if (!TRY_AWAIT(worker.store.isValidPath(newInfo.path))) continue;
            ValidPathInfo oldInfo(*TRY_AWAIT(worker.store.queryPathInfo(newInfo.path)));
            if (newInfo.narHash != oldInfo.narHash) {
                anyCheckMismatchSeen = true;
                if (settings.runDiffHook || settings.keepFailed) {
                    auto dst = worker.store.toRealPath(finalDestPath + checkSuffix);
                    deletePath(dst);
                    movePath(actualPath, dst);

                    TRY_AWAIT(handleDiffHook(
                        buildUser ? std::optional(buildUser->getUID()) : std::nullopt,
                        buildUser ? std::optional(buildUser->getGID()) : std::nullopt,
                        finalDestPath,
                        dst,
                        worker.store.printStorePath(drvPath),
                        tmpDir
                    ));

                    nondeterministic.push_back(std::make_pair(worker.store.toRealPath(finalDestPath), dst));
                } else
                    nondeterministic.push_back(std::make_pair(worker.store.toRealPath(finalDestPath), std::nullopt));
            }

            /* Since we verified the build, it's now ultimately trusted. */
            else if (!oldInfo.ultimate) {
                oldInfo.ultimate = true;
                localStore.signPathInfo(oldInfo);
                TRY_AWAIT(localStore.registerValidPaths({{oldInfo.path, oldInfo}}));
            }

            /* Don't register anything, since we already have the
               previous versions which we're comparing. */
            continue;
        }

        /* For debugging, print out the referenced and unreferenced paths. */
        for (auto & i : inputPaths) {
            if (references.count(i))
                debug("referenced input: '%1%'", worker.store.printStorePath(i));
            else
                debug("unreferenced input: '%1%'", worker.store.printStorePath(i));
        }

        // FIXME: combine with scanForReferences()
        TRY_AWAIT(localStore.optimisePath(actualPath, NoRepair));
        worker.markContentsGood(newInfo.path);

        newInfo.deriver = drvPath;
        newInfo.ultimate = true;
        localStore.signPathInfo(newInfo);

        finish(newInfo.path);

        /* If it's a CA path, register it right away. This is necessary if it
           isn't statically known so that we can safely unlock the path before
           the next iteration */
        if (newInfo.ca)
            TRY_AWAIT(localStore.registerValidPaths({{newInfo.path, newInfo}}));

        infos.emplace(outputName, std::move(newInfo));
    }

    if (buildMode == bmCheck) {
        if (!nondeterministic.empty()) {
            std::ostringstream msg;
            msg << HintFmt("derivation '%s' may not be deterministic: outputs differ", drvPath.to_string());
            for (auto [oldPath, newPath]: nondeterministic) {
                if (newPath) {
                    msg << HintFmt(
                        "\n  output differs: output '%s' differs from '%s'", oldPath, *newPath
                    );
                } else {
                    msg << HintFmt("\n  output '%s' differs", oldPath);
                }
            }
            throw NotDeterministic(msg.str());
        }
        /* In case of fixed-output derivations with hash mismatches,
           we don't want to rethrow the exception until later so that
           the unexpected path is still registered as valid. */
        if (!delayedException)
            co_return TRY_AWAIT(assertPathValidity());
    }

    /* Apply output checks. */
    TRY_AWAIT(checkOutputs(infos, alreadyRegisteredOutputs));

    /* Register each output path as valid, and register the sets of
       paths referenced by each of them.  If there are cycles in the
       outputs, this will fail. */
    {
        auto & localStore = getLocalStore();

        ValidPathInfos infos2;
        for (auto & [outputName, newInfo] : infos) {
            infos2.insert_or_assign(newInfo.path, newInfo);
        }
        TRY_AWAIT(localStore.registerValidPaths(infos2));
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
        builtOutputs.emplace(outputName, thisRealisation);
    }

    co_return builtOutputs;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> LocalDerivationGoal::checkOutputs(const std::map<std::string, ValidPathInfo> & newlyBuiltOutputs, const std::map<std::string, StorePath> & alreadyRegisteredOutputs)
try {
    std::map<Path, const ValidPathInfo &> outputsByPath;
    for (auto & output : newlyBuiltOutputs)
        outputsByPath.emplace(worker.store.printStorePath(output.second.path), output.second);

    for (auto & output : newlyBuiltOutputs) {
        auto & outputName = output.first;
        auto & info = output.second;

        struct Checks
        {
            bool ignoreSelfRefs = false;
            std::optional<uint64_t> maxSize, maxClosureSize;
            std::optional<Strings> allowedReferences, allowedRequisites, disallowedReferences, disallowedRequisites;
        };

        struct Closure {
            /** Keys: paths in the closure, values: reverse path from an initial path to the parent of the key */
            std::map<StorePath, StorePathSet> paths;
            uint64_t size;
        };

        /* Compute the closure and closure size of some output. This
           is slightly tricky because some of its references (namely
           other outputs) may not be valid yet. */
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto getClosure = [&](const StorePath & path
                          ) -> kj::Promise<Result<Closure>> {
            try {
                uint64_t closureSize = 0;
                std::map<StorePath, StorePathSet> pathsDone;
                std::queue<StorePath> pathsLeft;
                pathsLeft.push(path);

                while (!pathsLeft.empty()) {
                    auto path = pathsLeft.front();
                    pathsLeft.pop();
                    if (pathsDone.contains(path)) {
                        continue;
                    }

                    auto i = outputsByPath.find(worker.store.printStorePath(path));
                    auto & refs = pathsDone[path];
                    if (i != outputsByPath.end()) {
                        closureSize += i->second.narSize;
                        for (auto & ref : i->second.references) {
                            pathsLeft.push(ref);
                            refs.insert(ref);
                        }
                    } else {
                        auto info = TRY_AWAIT(worker.store.queryPathInfo(path));
                        closureSize += info->narSize;
                        for (auto & ref : info->references) {
                            pathsLeft.push(ref);
                            refs.insert(ref);
                        }
                    }
                }

                co_return Closure { std::move(pathsDone), closureSize};
            } catch (...) {
                co_return result::current_exception();
            }
        };

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto applyChecks = [&](const Checks & checks) -> kj::Promise<Result<void>> {
            try {
                if (checks.maxSize && info.narSize > *checks.maxSize)
                    throw BuildError("path '%s' is too large at %d bytes; limit is %d bytes",
                        worker.store.printStorePath(info.path), info.narSize, *checks.maxSize);

                if (checks.maxClosureSize) {
                    uint64_t closureSize = TRY_AWAIT(getClosure(info.path)).size;
                    if (closureSize > *checks.maxClosureSize)
                        throw BuildError("closure of path '%s' is too large at %d bytes; limit is %d bytes",
                            worker.store.printStorePath(info.path), closureSize, *checks.maxClosureSize);
                }

                // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
                auto checkRefs = [&](const std::optional<Strings> & value,
                                     bool allowed,
                                     bool recursive) -> kj::Promise<Result<void>> {
                    try {
                        if (!value) co_return result::success();

                        /* Parse a list of reference specifiers.  Each element must
                           either be a store path, or the symbolic name of the output
                           of the derivation (such as `out'). */
                        StorePathSet spec;
                        for (auto & i : *value) {
                            if (worker.store.isStorePath(i))
                                spec.insert(worker.store.parseStorePath(i));
                            else if (auto output = get(newlyBuiltOutputs, i))
                                spec.insert(output->path);
                            else if (auto storePath = get(alreadyRegisteredOutputs, i))
                                spec.insert(*storePath);
                            else {
                                std::string outputsListing = concatMapStringsSep(
                                    ", ",
                                    newlyBuiltOutputs,
                                    [](auto & o) { return o.first; }
                                );
                                if (!alreadyRegisteredOutputs.empty()) {
                                    outputsListing.append(outputsListing.empty() ? "" : ", ");
                                    outputsListing.append(concatMapStringsSep(
                                        ", ",
                                        alreadyRegisteredOutputs,
                                        [](auto & o) { return o.first; })
                                    );
                                }
                                throw BuildError("derivation '%s' output check for '%s' contains an illegal reference specifier '%s',"
                                    " expected store path or output name (one of [%s])",
                                    worker.store.printStorePath(drvPath), outputName, i, outputsListing);
                            }
                        }

                        std::map<StorePath, StorePathSet> used;
                        if (recursive) {
                            used = TRY_AWAIT(getClosure(info.path)).paths;
                        } else {
                            for (auto & ref : info.references) {
                                used.insert({ref, {}});
                            }
                        }

                        std::set<StorePath> badPaths;
                        for (auto & [path, refs] : used) {
                            if (path == info.path && recursive && checks.ignoreSelfRefs) {
                                continue;
                            }
                            if (allowed) {
                                if (!spec.count(path)) {
                                    badPaths.insert(path);
                                }
                            } else {
                                if (spec.count(path)) {
                                    badPaths.insert(path);
                                }
                            }
                        }

                        if (!badPaths.empty()) {
                            auto badPathsList = concatMapStringsSep(
                                "\n",
                                badPaths,
                                [&](const StorePath & i) -> std::string {
                                    return worker.store.printStorePath(i);
                                }
                            );
                            if (recursive) {
                                std::string badPathRefsTree;
                                for (auto & i : badPaths) {
                                    badPathRefsTree += TRY_AWAIT(genGraphString(
                                        info.path, i, used, worker.store, true, false
                                    ));
                                    badPathRefsTree += "\n";
                                }

                                throw BuildError(
                                    "output '%s' is not allowed to refer to the following "
                                    "paths:\n%s\n\nShown below are chains that lead to the "
                                    "forbidden path(s).\n%s",
                                    worker.store.printStorePath(info.path),
                                    badPathsList,
                                    Uncolored(badPathRefsTree)
                                );
                            } else {
                                throw BuildError(
                                    "output '%s' is not allowed to have direct references to the "
                                    "following paths:\n%s",
                                    worker.store.printStorePath(info.path),
                                    badPathsList
                                );
                            }
                        }
                        co_return result::success();
                    } catch (...) {
                        co_return result::current_exception();
                    }
                };

                TRY_AWAIT(checkRefs(checks.allowedReferences, true, false));
                TRY_AWAIT(checkRefs(checks.allowedRequisites, true, true));
                TRY_AWAIT(checkRefs(checks.disallowedReferences, false, false));
                TRY_AWAIT(checkRefs(checks.disallowedRequisites, false, true));
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        };

        if (auto structuredAttrs = parsedDrv->getStructuredAttrs()) {
            if (get(*structuredAttrs, "allowedReferences")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute "
                    "'allowedReferences'; use 'outputChecks' instead"
                );
            }
            if (get(*structuredAttrs, "allowedRequisites")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute "
                    "'allowedRequisites'; use 'outputChecks' instead"
                );
            }
            if (get(*structuredAttrs, "disallowedRequisites")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute "
                    "'disallowedRequisites'; use 'outputChecks' instead"
                );
            }
            if (get(*structuredAttrs, "disallowedReferences")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute "
                    "'disallowedReferences'; use 'outputChecks' instead"
                );
            }
            if (get(*structuredAttrs, "maxSize")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute 'maxSize'; "
                    "use 'outputChecks' instead"
                );
            }
            if (get(*structuredAttrs, "maxClosureSize")){
                printTaggedWarning(
                    "'structuredAttrs' disables the effect of the top-level attribute "
                    "'maxClosureSize'; use 'outputChecks' instead"
                );
            }
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

                    TRY_AWAIT(applyChecks(checks));
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
            TRY_AWAIT(applyChecks(checks));
        }
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

// make `entry` in `parentFd` visible to the given user and group, preserving
// inode modes as much as possible. if the builder sets the mode of any inode
// to not be readable by the owner we keep this; not doing so could interfere
// with error analysis. if the builder used multiple uids or gids we will not
// keep them around and instead collapse them all onto the uid/gid given here
// to not leave around inodes owned by unassigned uids/gids in the system. we
// also clear setuid/setgid/sticky bits just to be safe even though a builder
// should not be able to set them to begin, otherwise we may leave setuid/gid
// executables in the tree even with user/group set to -1/-1. there have been
// enough bugs of this kind in the past to warrant some extra attention here.
static void makeVisible(int parentFd, const char * entry, uid_t user, gid_t group)
{
    struct stat st;
    // NOLINTNEXTLINE(lix-unsafe-c-calls): entry is a dentry name
    if (fstatat(parentFd, entry, &st, AT_SYMLINK_NOFOLLOW)) {
        throw SysError("fstat(%s)", guessOrInventPathFromFD(parentFd));
    }
    if (S_ISDIR(st.st_mode)) {
        auto dirfd = sys::openat(parentFd, entry, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (!dirfd) {
            throw SysError("openat(%s/%s)", guessOrInventPathFromFD(parentFd), entry);
        }
        AutoCloseDir dir(fdopendir(dirfd.get()));
        if (!dir) {
            throw SysError("fdopendir(%s/%s)", guessOrInventPathFromFD(parentFd), entry);
        }
        dirfd.release();

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0) {
                continue;
            }
            makeVisible(::dirfd(dir.get()), dirent->d_name, user, group);
        }
    }

    // ignore permissions errors for symlinks. linux can't chmod them.
    // clear special permission bits while we're here, just to be safe
    if (sys::fchmodat(parentFd, entry, st.st_mode & 0777, AT_SYMLINK_NOFOLLOW)
        && !S_ISLNK(st.st_mode))
    {
        throw SysError("fchmod(%s)", guessOrInventPathFromFD(parentFd));
    }
    if (user != uid_t(-1) && group != gid_t(-1)
        && sys::fchownat(parentFd, entry, user, group, AT_SYMLINK_NOFOLLOW))
    {
        throw SysError("fchown(%s)", guessOrInventPathFromFD(parentFd));
    }
}

void LocalDerivationGoal::finalizeTmpDir(bool force, bool duringDestruction)
{
    if (tmpDirRoot != "") {
        /* Don't keep temporary directories for builtins because they
           might have privileged stuff (like a copy of netrc). */
        if (settings.keepFailed && !force && !drv->isBuiltin()) {
            printError("note: keeping build directory '%s'", tmpDirRoot);
            try {
                // always make visible, but don't always chown. if we run as
                // root we may not want to chown things to root:root so much
                auto creds = worker.store.associatedCredentials();
                makeVisible(
                    tmpDirFd.get(), ".", creds ? creds->user : -1, creds ? creds->group : -1
                );
            } catch (SysError & e) {
                printError("error making '%s' accessible: %s", tmpDir, e.what());
            }
            (void) sys::chmod(tmpDirRoot, 0755);
        }
        else if (duringDestruction)
            deletePathUninterruptible(tmpDirRoot);
        else
            deletePath(tmpDirRoot);
        tmpDirRoot = "";
    }
}


StorePath LocalDerivationGoal::makeFallbackPath(OutputNameView outputName)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":name:" + std::string(outputName),
        Hash(HashType::SHA256), outputPathName(drv->name, outputName));
}


StorePath LocalDerivationGoal::makeFallbackPath(const StorePath & path)
{
    return worker.store.makeStorePath(
        "rewrite:" + std::string(drvPath.to_string()) + ":" + std::string(path.to_string()),
        Hash(HashType::SHA256), path.name());
}

kj::Promise<Result<Goal::WorkResult>> LocalDerivationGoal::handleRawChild() noexcept
try {
    if (auto error = TRY_AWAIT(handleRawChildStream())) {
        co_return std::move(*error);
    }
    co_return TRY_AWAIT(buildDone());
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<Goal::WorkResult>>>
LocalDerivationGoal::handleRawChildStream() noexcept
try {
    AsyncFdIoStream in(AsyncFdIoStream::shared_fd{}, builderOutPTY.get());

    std::map<ActivityId, Activity> builderActivities;
    LogLineSplitter splitter;

    auto act = logger->startActivity(
        lvlInfo,
        actBuild,
        buildDescription(),
        Logger::Fields{worker.store.printStorePath(drvPath), "", 1, 1}
    );

    auto flushLine = [&](const std::string & line) {
        if (const auto state =
                handleJSONLogMessage(line, act, builderActivities, "the derivation builder"))
        {
            return *state;
        } else {
            logTail.push_back(line);
            if (logTail.size() > settings.logLines) {
                logTail.pop_front();
            }

            return act.result(resBuildLogLine, line);
        }
    };

    auto buf = kj::heapArray<char>(4096);
    while (true) {
        std::string_view data;
        try {
            if (const auto got = TRY_AWAIT(in.read(buf.begin(), buf.size()))) {
                data = {buf.begin(), *got};
            } else {
                co_return std::nullopt;
            }
        } catch (SysError & e) {
            // the builder output stream may be a pty fd, and closing one pty
            // endpoint sends EIO to the other endpoint. this is a good exit.
            if (e.errNo == EIO) {
                data = {};
            } else {
                throw;
            }
        }
        lastChildActivity = AIO().provider.getTimer().now();

        if (data.empty()) {
            if (auto left = splitter.finish(); !left.empty()) {
                if (flushLine(left) == Logger::BufferState::NeedsFlush) {
                    TRY_AWAIT(act.getLogger().flush());
                }
            }
            co_return std::nullopt;
        }

        if (logSink) {
            (*logSink)(data);
        }

        while (!data.empty()) {
            if (auto line = splitter.feed(data)) {
                if (flushLine(*line) == Logger::BufferState::NeedsFlush) {
                    TRY_AWAIT(act.getLogger().flush());
                }
            }
        }
    }
} catch (...) {
    co_return result::current_exception();
}
}
