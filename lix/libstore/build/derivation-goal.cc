#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/build/hook-instance.hh"
#include "lix/libstore/build/worker.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/json.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh" // IWYU pragma: keep
#include "lix/libstore/local-store.hh" // TODO remove, along with remaining downcasts
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/strings.hh"
#include "lix/libstore/build/hook-instance.capnp.h"
#include "lix/libstore/types-rpc.hh"
#include "lix/libutil/types-rpc.hh"

#include <boost/outcome/try.hpp>
#include <capnp/rpc-twoparty.h>
#include <fstream>
#include <kj/array.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/vector.h>
#include <optional>
#include <ranges>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/resource.h>

#if HAVE_STATVFS
#include <sys/statvfs.h>
#endif

/* Includes required for chroot support. */
#if __linux__
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/mman.h>
#include <sched.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#if HAVE_SECCOMP
#include <seccomp.h>
#endif
#define pivot_root(new_root, put_old) (syscall(SYS_pivot_root, new_root, put_old))
#endif

#if __APPLE__
#include <spawn.h>
#include <sys/sysctl.h>
#endif

#include <pwd.h>
#include <grp.h>

namespace nix {

DerivationGoal::DerivationGoal(const StorePath & drvPath,
    const OutputsSpec & wantedOutputs, Worker & worker, bool isDependency, BuildMode buildMode)
    : Goal(worker, isDependency)
    , useDerivation(true)
    , drvPath(drvPath)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    name = fmt(
        "building of '%s' from .drv file",
        DerivedPath::Built { makeConstantStorePath(drvPath), wantedOutputs }.to_string(worker.store));
    trace("created");

    mcExpectedBuilds = worker.expectedBuilds.addTemporarily(1);
}


DerivationGoal::DerivationGoal(DrvHasRoot, const StorePath & drvPath, const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs, Worker & worker, bool isDependency, BuildMode buildMode)
    : Goal(worker, isDependency)
    , useDerivation(false)
    , drvPath(drvPath)
    , wantedOutputs(wantedOutputs)
    , buildMode(buildMode)
{
    this->drv = std::make_unique<Derivation>(drv);

    name = fmt(
        "building of '%s' from in-memory derivation",
        DerivedPath::Built { makeConstantStorePath(drvPath), drv.outputNames() }.to_string(worker.store));
    trace("created");

    mcExpectedBuilds = worker.expectedBuilds.addTemporarily(1);
}


DerivationGoal::~DerivationGoal() noexcept(false)
{
    /* Careful: we should never ever throw an exception from a
       destructor. */
    try { closeLogFile(); } catch (...) { ignoreExceptionInDestructor(); }
}


void DerivationGoal::killChild()
{
    hook.reset();
    builderOutFD = nullptr;
}


Goal::WorkResult DerivationGoal::timedOut(Error && ex)
{
    killChild();
    return done(BuildResult::TimedOut, {}, std::move(ex));
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::workImpl() noexcept
{
    KJ_DEFER({
        act.reset();
        actLock.reset();
        builderActivities.clear();
    });

    BOOST_OUTCOME_CO_TRY(auto result, co_await (useDerivation ? getDerivation() : haveDerivation()));
    result.storePath = drvPath;
    co_return result;
}

bool DerivationGoal::addWantedOutputs(const OutputsSpec & outputs)
{
    if (isDone) {
        return false;
    }

    auto newWanted = wantedOutputs.union_(outputs);
    switch (needRestart) {
    case NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed:
        if (!newWanted.isSubsetOf(wantedOutputs))
            needRestart = NeedRestartForMoreOutputs::OutputsAddedDoNeed;
        break;
    case NeedRestartForMoreOutputs::OutputsAddedDoNeed:
        /* No need to check whether we added more outputs, because a
           restart is already queued up. */
        break;
    case NeedRestartForMoreOutputs::BuildInProgressWillNotNeed:
        /* We are already building all outputs, so it doesn't matter if
           we now want more. */
        break;
    };
    wantedOutputs = newWanted;
    return true;
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::getDerivation() noexcept
try {
    trace("init");

    /* The first thing to do is to make sure that the derivation
       exists.  If it doesn't, it may be created through a
       substitute. */
    if (buildMode == bmNormal && TRY_AWAIT(worker.evalStore.isValidPath(drvPath))) {
        co_return co_await loadDerivation();
    }

    TRY_AWAIT(waitForGoals(worker.goalFactory().makePathSubstitutionGoal(drvPath)));
    co_return co_await loadDerivation();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::loadDerivation() noexcept
try {
    trace("loading derivation");

    if (nrFailed != 0) {
        co_return done(
            BuildResult::MiscFailure,
            {},
            Error("cannot build missing derivation '%s'", worker.store.printStorePath(drvPath))
        );
    }

    /* `drvPath' should already be a root, but let's be on the safe
       side: if the user forgot to make it a root, we wouldn't want
       things being garbage collected while we're busy. */
    TRY_AWAIT(worker.evalStore.addTempRoot(drvPath));

    /* Get the derivation. It is probably in the eval store, but it might be inthe main store:

         - Resolved derivation are resolved against main store realisations, and so must be stored there.
     */
    for (auto * drvStore : { &worker.evalStore, &worker.store }) {
        if (TRY_AWAIT(drvStore->isValidPath(drvPath))) {
            drv = std::make_unique<Derivation>(TRY_AWAIT(drvStore->readDerivation(drvPath)));
            break;
        }
    }
    assert(drv);

    co_return TRY_AWAIT(haveDerivation());
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::haveDerivation() noexcept
try {
    trace("have derivation");

    parsedDrv = std::make_unique<ParsedDerivation>(drvPath, *drv);

    for (auto & i : drv->outputsAndPaths(worker.store))
        TRY_AWAIT(worker.store.addTempRoot(i.second.second));

    auto outputHashes = TRY_AWAIT(staticOutputHashes(worker.evalStore, *drv));
    for (auto & [outputName, outputHash] : outputHashes)
        initialOutputs.insert({
            outputName,
            InitialOutput {
                .wanted = true, // Will be refined later
                .outputHash = outputHash
            }
        });

    /* Check what outputs paths are not already valid. */
    auto [allValid, validOutputs] = TRY_AWAIT(checkPathValidity());

    /* If they are all valid, then we're done. */
    if (allValid && buildMode == bmNormal) {
        co_return done(BuildResult::AlreadyValid, std::move(validOutputs));
    }

    /* We are first going to try to create the invalid output paths
       through substitutes.  If that doesn't work, we'll build
       them. */
    kj::Vector<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies;
    if (settings.useSubstitutes) {
        if (parsedDrv->substitutesAllowed()) {
            for (auto & [outputName, status] : initialOutputs) {
                if (!status.wanted) continue;
                if (!status.known) {
                    // TODO remove somehow
                    throw Error(
                        "congrats, you hit vestigial CA code. sigh.\n"
                        "please report a bug at https://git.lix.systems/lix-project/lix/issues"
                    );
                } else {
                    auto * cap = getDerivationCA(*drv);
                    dependencies.add(worker.goalFactory().makePathSubstitutionGoal(
                        status.known->path,
                        buildMode == bmRepair ? Repair : NoRepair,
                        cap ? std::optional { *cap } : std::nullopt));
                }
            }
        } else {
            trace("skipping substitute because allowSubstitutes is false");
        }
    }

    if (!dependencies.empty()) { /* to prevent hang (no wake-up event) */
        TRY_AWAIT(waitForGoals(dependencies.releaseAsArray()));
    }
    co_return co_await outputsSubstitutionTried();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::outputsSubstitutionTried() noexcept
try {
    trace("all outputs substituted (maybe)");

    if (nrFailed > 0 && nrFailed > nrNoSubstituters + nrIncompleteClosure && !settings.tryFallback)
    {
        co_return done(
            BuildResult::TransientFailure,
            {},
            Error(
                "some substitutes for the outputs of derivation '%s' failed (usually happens due "
                "to networking issues); try '--fallback' to build derivation from source ",
                worker.store.printStorePath(drvPath)
            )
        );
    }

    /*  If the substitutes form an incomplete closure, then we should
        build the dependencies of this derivation, but after that, we
        can still use the substitutes for this derivation itself.

        If the nrIncompleteClosure != nrFailed, we have another issue as well.
        In particular, it may be the case that the hole in the closure is
        an output of the current derivation, which causes a loop if retried.
     */
    {
        bool substitutionFailed =
            nrIncompleteClosure > 0 &&
            nrIncompleteClosure == nrFailed;
        switch (retrySubstitution) {
        case RetrySubstitution::NoNeed:
            if (substitutionFailed)
                retrySubstitution = RetrySubstitution::YesNeed;
            break;
        case RetrySubstitution::YesNeed:
            // Should not be able to reach this state from here.
            assert(false);
            break;
        case RetrySubstitution::AlreadyRetried:
            debug("substitution failed again, but we already retried once. Not retrying again.");
            break;
        }
    }

    nrFailed = nrNoSubstituters = nrIncompleteClosure = 0;

    if (needRestart == NeedRestartForMoreOutputs::OutputsAddedDoNeed) {
        needRestart = NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed;
        co_return TRY_AWAIT(haveDerivation());
    }

    auto [allValid, validOutputs] = TRY_AWAIT(checkPathValidity());

    // recheck needRestart. more wanted outputs may have been added during the
    // path validity check, and we do not want to treat !allValid as an error.
    if (!allValid && needRestart == NeedRestartForMoreOutputs::OutputsAddedDoNeed) {
        needRestart = NeedRestartForMoreOutputs::OutputsUnmodifedDontNeed;
        co_return TRY_AWAIT(haveDerivation());
    }

    if (buildMode == bmNormal && allValid) {
        co_return done(BuildResult::Substituted, std::move(validOutputs));
    }
    if (buildMode == bmRepair && allValid) {
        co_return TRY_AWAIT(repairClosure());
    }
    if (buildMode == bmCheck && !allValid)
        throw Error("some outputs of '%s' are not valid, so checking is not possible",
            worker.store.printStorePath(drvPath));

    /* Nothing to wait for; tail call */
    co_return TRY_AWAIT(gaveUpOnSubstitution());
} catch (...) {
    co_return result::current_exception();
}


/* At least one of the output paths could not be
   produced using a substitute.  So we have to build instead. */
kj::Promise<Result<Goal::WorkResult>> DerivationGoal::gaveUpOnSubstitution() noexcept
try {
    kj::Vector<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies;

    /* At this point we are building all outputs, so if more are wanted there
       is no need to restart. */
    needRestart = NeedRestartForMoreOutputs::BuildInProgressWillNotNeed;

    /* The inputs must be built before we can build this goal. */
    inputDrvOutputs.clear();
    if (useDerivation) {
        auto addWaiteeDerivedPath = [&](DerivedPathOpaque inputDrv, const StringSet & inputNode) {
            if (!inputNode.empty())
                dependencies.add(worker.goalFactory().makeGoal(
                    DerivedPath::Built {
                        .drvPath = inputDrv,
                        .outputs = inputNode,
                    },
                    buildMode == bmRepair ? bmRepair : bmNormal));
        };

        for (const auto & [inputDrvPath, inputNode] : dynamic_cast<Derivation *>(drv.get())->inputDrvs) {
            addWaiteeDerivedPath(makeConstantStorePath(inputDrvPath), inputNode);
        }
    }

    /* Copy the input sources from the eval store to the build
       store.

       Note that some inputs might not be in the eval store because they
       are (resolved) derivation outputs in a resolved derivation. */
    if (&worker.evalStore != &worker.store) {
        RealisedPath::Set inputSrcs;
        for (auto & i : drv->inputSrcs)
            if (TRY_AWAIT(worker.evalStore.isValidPath(i)))
                inputSrcs.insert(i);
        TRY_AWAIT(copyClosure(worker.evalStore, worker.store, inputSrcs));
    }

    for (auto & i : drv->inputSrcs) {
        if (TRY_AWAIT(worker.store.isValidPath(i))) continue;
        if (!settings.useSubstitutes)
            throw Error("dependency '%s' of '%s' does not exist, and substitution is disabled",
                worker.store.printStorePath(i), worker.store.printStorePath(drvPath));
        dependencies.add(worker.goalFactory().makePathSubstitutionGoal(i));
    }

    if (!dependencies.empty()) {/* to prevent hang (no wake-up event) */
        TRY_AWAIT(waitForGoals(dependencies.releaseAsArray()));
    }
    co_return co_await inputsRealised();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::repairClosure() noexcept
try {
    /* If we're repairing, we now know that our own outputs are valid.
       Now check whether the other paths in the outputs closure are
       good.  If not, then start derivation goals for the derivations
       that produced those outputs. */

    /* Get the output closure. */
    auto outputs = TRY_AWAIT(queryDerivationOutputMap());
    StorePathSet outputClosure;
    for (auto & i : outputs) {
        if (!wantedOutputs.contains(i.first)) continue;
        TRY_AWAIT(worker.store.computeFSClosure(i.second, outputClosure));
    }

    /* Filter out our own outputs (which we have already checked). */
    for (auto & i : outputs)
        outputClosure.erase(i.second);

    /* Get all dependencies of this derivation so that we know which
       derivation is responsible for which path in the output
       closure. */
    StorePathSet inputClosure;
    if (useDerivation) TRY_AWAIT(worker.store.computeFSClosure(drvPath, inputClosure));
    std::map<StorePath, StorePath> outputsToDrv;
    for (auto & i : inputClosure)
        if (i.isDerivation()) {
            auto depOutputs =
                TRY_AWAIT(worker.store.queryDerivationOutputMap(i, &worker.evalStore));
            for (auto & j : depOutputs)
                outputsToDrv.insert_or_assign(j.second, i);
        }

    /* Check each path (slow!). */
    kj::Vector<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies;
    for (auto & i : outputClosure) {
        if (TRY_AWAIT(worker.pathContentsGood(i))) continue;
        printError(
            "found corrupted or missing path '%s' in the output closure of '%s'",
            worker.store.printStorePath(i), worker.store.printStorePath(drvPath));
        auto drvPath2 = outputsToDrv.find(i);
        if (drvPath2 == outputsToDrv.end())
            dependencies.add(worker.goalFactory().makePathSubstitutionGoal(i, Repair));
        else
            dependencies.add(worker.goalFactory().makeGoal(
                DerivedPath::Built {
                    .drvPath = makeConstantStorePath(drvPath2->second),
                    .outputs = OutputsSpec::All { },
                },
                bmRepair));
    }

    if (dependencies.empty()) {
        // NOTE assertPathValidity *can* fail if wanted outputs are added while
        // it is running. repair mode cannot work correctly if the goal was not
        // created with all outputs wanted in the first place though, so we can
        // ignore this possiblity and assume that all failures are real errors.
        co_return done(BuildResult::AlreadyValid, TRY_AWAIT(assertPathValidity()));
    }

    TRY_AWAIT(waitForGoals(dependencies.releaseAsArray()));
    co_return co_await closureRepaired();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::closureRepaired() noexcept
try {
    trace("closure repaired");
    if (nrFailed > 0)
        throw Error("some paths in the output closure of derivation '%s' could not be repaired",
            worker.store.printStorePath(drvPath));
    co_return done(BuildResult::AlreadyValid, TRY_AWAIT(assertPathValidity()));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::inputsRealised() noexcept
try {
    trace("all inputs realised");

    if (nrFailed != 0) {
        if (!useDerivation)
            throw Error("some dependencies of '%s' are missing", worker.store.printStorePath(drvPath));
        co_return done(
            BuildResult::DependencyFailed,
            {},
            Error(
                "%s dependencies of derivation '%s' failed to build",
                nrFailed,
                worker.store.printStorePath(drvPath)
            )
        );
    }

    if (retrySubstitution == RetrySubstitution::YesNeed) {
        retrySubstitution = RetrySubstitution::AlreadyRetried;
        co_return co_await haveDerivation();
    }

    /* Gather information necessary for computing the closure and/or
       running the build hook. */

    /* Determine the full set of input paths. */

    /* First, the input derivations. */
    if (useDerivation) {
        auto & fullDrv = *dynamic_cast<Derivation *>(drv.get());

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        auto accumInputPaths = [&](const StorePath & depDrvPath, const StringSet & inputNode) -> kj::Promise<Result<void>> {
            try {
                /* Add the relevant output closures of the input derivation
                   `i' as input paths.  Only add the closures of output paths
                   that are specified as inputs. */
                // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
                auto getOutput = [&](const std::string & outputName
                                 ) -> kj::Promise<Result<StorePath>> {
                    /* TODO (impure derivations-induced tech debt):
                       Tracking input derivation outputs statefully through the
                       goals is error prone and has led to bugs.
                       For a robust nix, we need to move towards the `else` branch,
                       which does not rely on goal state to match up with the
                       reality of the store, which is our real source of truth.
                       However, the impure derivations feature still relies on this
                       fragile way of doing things, because its builds do not have
                       a representation in the store, which is a usability problem
                       in itself. When implementing this logic entirely with lookups
                       make sure that they're cached. */
                    try {
                        if (auto outPath = get(inputDrvOutputs, { depDrvPath, outputName })) {
                            co_return *outPath;
                        }
                        else {
                            auto outMap = TRY_AWAIT(worker.evalStore.isValidPath(depDrvPath))
                                ? TRY_AWAIT(worker.store.queryDerivationOutputMap(depDrvPath, &worker.evalStore))
                                : TRY_AWAIT(worker.store.isValidPath(depDrvPath))
                                ? TRY_AWAIT(worker.store.queryDerivationOutputMap(depDrvPath, &worker.store))
                                : (assert(false), OutputPathMap{});

                            auto outMapPath = outMap.find(outputName);
                            if (outMapPath == outMap.end()) {
                                throw Error(
                                    "derivation '%s' requires non-existent output '%s' from input derivation '%s'",
                                    worker.store.printStorePath(drvPath), outputName, worker.store.printStorePath(depDrvPath));
                            }
                            co_return outMapPath->second;
                        }
                    } catch (...) {
                        co_return result::current_exception();
                    }
                };

                for (auto & outputName : inputNode) {
                    TRY_AWAIT(
                        worker.store.computeFSClosure(TRY_AWAIT(getOutput(outputName)), inputPaths)
                    );
                }

                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        };

        for (auto & [depDrvPath, depNode] : fullDrv.inputDrvs)
            TRY_AWAIT(accumInputPaths(depDrvPath, depNode));
    }

    /* Second, the input sources. */
    TRY_AWAIT(worker.store.computeFSClosure(drv->inputSrcs, inputPaths));

    debug("added input paths %s", worker.store.showPaths(inputPaths));

    /* What type of derivation are we building? */
    derivationType = drv->type();

    /* Okay, try to build.  Note that here we don't wait for a build
       slot to become available, since we don't need one if there is a
       build hook. */
    co_return co_await tryToBuild();
} catch (...) {
    co_return result::current_exception();
}

void DerivationGoal::started()
{
    auto msg = fmt(
        buildMode == bmRepair ? "repairing outputs of '%s'" :
        buildMode == bmCheck ? "checking outputs of '%s'" :
        "building '%s'", worker.store.printStorePath(drvPath));
    fmt("building '%s'", worker.store.printStorePath(drvPath));
    if (hook) msg += fmt(" on '%s'", machineName);
    act = std::make_unique<Activity>(*logger, lvlInfo, actBuild, msg,
        Logger::Fields{worker.store.printStorePath(drvPath), hook ? machineName : "", 1, 1});
    mcRunningBuilds = worker.runningBuilds.addTemporarily(1);
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::tryToBuild() noexcept
try {
retry:
    trace("trying to build");

    /* Obtain locks on all output paths, if the paths are known a priori.

       The locks are automatically released when we exit this function or Nix
       crashes.  If we can't acquire the lock, then continue; hopefully some
       other goal can start a build, and if not, the main loop will sleep a few
       seconds and then retry this goal. */
    PathSet lockFiles;
    if (dynamic_cast<LocalStore *>(&worker.store)) {
        /* If we aren't a local store, we might need to use the local store as
           a build remote, but that would cause a deadlock. */
        /* FIXME: Make it so we can use ourselves as a build remote even if we
           are the local store (separate locking for building vs scheduling? */
        /* FIXME: find some way to lock for scheduling for the other stores so
           a forking daemon with --store still won't farm out redundant builds.
           */
        for (auto & i : drv->outputsAndPaths(worker.store)) {
            lockFiles.insert(worker.store.Store::toRealPath(i.second.second));
        }
    }

    outputLocks = tryLockPaths(lockFiles);
    if (!outputLocks) {
        if (!actLock)
            actLock = std::make_unique<Activity>(*logger, lvlWarn, actBuildWaiting,
                fmt("waiting for lock on %s", Magenta(showPaths(lockFiles))));
        co_await waitForAWhile();
        // we can loop very often, and `co_return co_await` always allocates a new frame
        goto retry;
    }

    actLock.reset();

    /* Now check again whether the outputs are valid.  This is because
       another process may have started building in parallel.  After
       it has finished and released the locks, we can (and should)
       reuse its results.  (Strictly speaking the first check can be
       omitted, but that would be less efficient.)  Note that since we
       now hold the locks on the output paths, no other process can
       build this derivation, so no further checks are necessary. */
    auto [allValid, validOutputs] = TRY_AWAIT(checkPathValidity());

    if (buildMode != bmCheck && allValid) {
        debug("skipping build of derivation '%s', someone beat us to it", worker.store.printStorePath(drvPath));
        co_return done(BuildResult::AlreadyValid, std::move(validOutputs));
    }

    /* If any of the outputs already exist but are not valid, delete
       them. */
    for (auto & [_, status] : initialOutputs) {
        if (!status.known || status.known->isValid()) continue;
        auto storePath = status.known->path;
        debug("removing invalid path '%s'", worker.store.printStorePath(status.known->path));
        deletePath(worker.store.Store::toRealPath(storePath));
    }

    /* Don't do a remote build if the derivation has the attribute
       `preferLocalBuild' set.  Also, check and repair modes are only
       supported for local builds. */
    bool buildLocally =
        (buildMode != bmNormal || parsedDrv->willBuildLocally(worker.store))
        && settings.maxBuildJobs.get() != 0;

    if (!buildLocally) {
        auto hookReply = TRY_AWAIT(tryBuildHook());
        switch (hookReply.index()) {
        case 0: {
            HookResult::Accept & a = std::get<0>(hookReply);
            co_return std::move(a.result);
        }

        case 1: {
            HookResult::Decline _ = std::get<1>(hookReply);
            break;
        }

        case 2: {
            HookResult::Postpone _ = std::get<2>(hookReply);
            /* Not now; wait until at least one child finishes or
                the wake-up timeout expires. */
            if (!actLock)
                actLock = std::make_unique<Activity>(*logger, lvlTalkative, actBuildWaiting,
                    fmt("waiting for a machine to build '%s'", Magenta(worker.store.printStorePath(drvPath))));
            outputLocks.reset();
            co_await waitForAWhile();
            goto retry;
        }

        default:
            // can't static_assert this because HookReply *subclasses* variant and std::variant_size breaks
            assert(false && "unexpected hook reply");
        }
    }

    actLock.reset();

    co_return co_await tryLocalBuild();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::tryLocalBuild() noexcept
try {
    throw Error(
        "unable to build with a primary store that isn't a local store; "
        "either pass a different '--store' or enable remote builds."
        "\nhttps://docs.lix.systems/manual/lix/stable/advanced-topics/distributed-builds.html");
} catch (...) {
    return {result::current_exception()};
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


void replaceValidPath(const Path & storePath, const Path & tmpPath)
{
    /* We can't atomically replace storePath (the original) with
       tmpPath (the replacement), so we have to move it out of the
       way first.  We'd better not be interrupted here, because if
       we're repairing (say) Glibc, we end up with a broken system. */
    Path oldPath;
    if (pathExists(storePath)) {
        do {
            oldPath = makeTempPath(storePath, ".old");
            // store paths are often directories so we can't just unlink() it
            // let's make sure the path doesn't exist before we try to use it
        } while (pathExists(oldPath));
        movePath(storePath, oldPath);
    }

    try {
        movePath(tmpPath, storePath);
    } catch (...) {
        try {
            // attempt to recover
            if (!oldPath.empty())
                movePath(oldPath, storePath);
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
        throw;
    }

    if (!oldPath.empty())
        deletePath(oldPath);
}


int DerivationGoal::getChildStatus()
{
    builderOutFD = nullptr;
    return hook->pid.kill();
}


void DerivationGoal::closeReadPipes()
{
    hook->fromHook.reset();
    builderOutFD = nullptr;
}

void DerivationGoal::cleanupHookFinally()
{
}


void DerivationGoal::cleanupPreChildKill()
{
}


void DerivationGoal::cleanupPostChildKill()
{
}


bool DerivationGoal::cleanupDecideWhetherDiskFull()
{
    return false;
}


void DerivationGoal::cleanupPostOutputsRegisteredModeCheck()
{
}


void DerivationGoal::cleanupPostOutputsRegisteredModeNonCheck()
{
}

static kj::Promise<Result<void>> runPostBuildHook(
    Store & store, Logger & logger, const StorePath & drvPath, const StorePathSet & outputPaths
)
try {
    auto hook = settings.postBuildHook;
    if (hook == "") {
        co_return result::success();
    }

    Activity act(
        logger,
        lvlTalkative,
        actPostBuildHook,
        fmt("running post-build-hook '%s'", settings.postBuildHook),
        Logger::Fields{store.printStorePath(drvPath)}
    );
    std::map<std::string, std::string> hookEnvironment = getEnv();

    auto drvPathPretty = store.printStorePath(drvPath);
    hookEnvironment.emplace("DRV_PATH", drvPathPretty);
    hookEnvironment.emplace("OUT_PATHS", chomp(concatStringsSep(" ", store.printStorePathSet(outputPaths))));
    hookEnvironment.emplace("NIX_CONFIG", globalConfig.toKeyValue(true));

    auto proc = runProgram2({
        .program = settings.postBuildHook,
        .environment = hookEnvironment,
        .captureStdout = true,
        .redirections = {{.dup = STDERR_FILENO, .from = STDOUT_FILENO}},
    });
    auto wait = kj::defer([&] {
        try {
            proc.waitAndCheck();
        } catch (nix::Error & e) {
            e.addTrace(nullptr,
                "while running the post-build-hook %s for derivation %s",
                settings.postBuildHook,
                drvPathPretty
            );
            throw;
        }
    });

    auto & hookStdout = *proc.getStdout();
    std::string currentLine;
    std::vector<char> buffer(8192);
    while (const auto got = TRY_AWAIT(hookStdout.readRange(buffer.data(), 1, buffer.size()))) {
        const std::string_view data{buffer.data(), *got};
        for (auto c : data) {
            if (c == '\n') {
                act.result(resPostBuildLogLine, currentLine);
                currentLine.clear();
            } else {
                currentLine += c;
            }
        }
    }

    if (currentLine != "") {
        currentLine += '\n';
        act.result(resPostBuildLogLine, currentLine);
    }

    wait.run();
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

std::string DerivationGoal::buildErrorContents(const std::string & exitMsg, bool diskFull)
{
    auto msg = fmt("builder for '%s' %s", Magenta(worker.store.printStorePath(drvPath)), exitMsg);

    if (!logger->isVerbose() && !logTail.empty()) {
        msg += fmt(";\nlast %d log lines:\n", logTail.size());
        for (auto & line : logTail) {
            msg += "> ";
            msg += line;
            msg += "\n";
        }
        auto nixLogCommand =
            experimentalFeatureSettings.isEnabled(Xp::NixCommand) ? "nix log" : "nix-store -l";
        msg +=
            fmt("For full logs, run:\n\t" ANSI_BOLD "%s %s" ANSI_NORMAL "",
                nixLogCommand,
                worker.store.printStorePath(drvPath));
    }

    if (diskFull) {
        msg += "\nnote: build failure may have been caused by lack of free disk space";
    }

    return msg;
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::buildDone(std::shared_ptr<Error> remoteError
) noexcept
try {
    trace("build done");

    slotToken = {};
    Finally releaseBuildUser([&](){ this->cleanupHookFinally(); });

    cleanupPreChildKill();

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int rawStatus = getChildStatus();
    const auto [exited, exitCode, exitMsg] = [&]() -> std::tuple<bool, int, std::string> {
        // override exit status with 1 if we received an exception via rpc for
        // historical reasons: the build hook used to turn build errors into a
        // log line and an `exit(1)` previously, now it returns the full error
        if (remoteError) {
            return {true, 1, "failed on remote builder"};
        } else {
            if (WIFEXITED(rawStatus)) {
                return {true, WEXITSTATUS(rawStatus), statusToString(rawStatus)};
            } else {
                return {false, -1, statusToString(rawStatus)};
            }
        }
    }();

    debug("builder process for '%s' finished", worker.store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(0);

    /* Close the read side of the logger pipe. */
    closeReadPipes();

    /* Close the log file. */
    closeLogFile();

    cleanupPostChildKill();

    if (buildResult.cpuUser && buildResult.cpuSystem) {
        debug(
            "builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
            worker.store.printStorePath(drvPath),
            rawStatus,
            ((double) buildResult.cpuUser->count()) / 1000000,
            ((double) buildResult.cpuSystem->count()) / 1000000
        );
    }

    bool diskFull = false;
    try {

        /* Check the exit status. */
        if (!exited || exitCode != 0) {
            diskFull |= cleanupDecideWhetherDiskFull();
            throw BuildError(buildErrorContents(exitMsg, diskFull));
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        auto builtOutputs = TRY_AWAIT(registerOutputs());

        StorePathSet outputPaths;
        for (auto & [_, output] : builtOutputs)
            outputPaths.insert(output.outPath);
        TRY_AWAIT(runPostBuildHook(worker.store, *logger, drvPath, outputPaths));

        cleanupPostOutputsRegisteredModeNonCheck();

        /* It is now safe to delete the lock files, since all future
           lockers will see that the output paths are valid; they will
           not create new lock files with the same names as the old
           (unlinked) lock files. */
        outputLocks.reset();

        co_return done(BuildResult::Built, std::move(builtOutputs));
    } catch (BuildError & e) {
        outputLocks.reset();

        BuildResult::Status st = BuildResult::MiscFailure;

        if (hook && exited && exitCode == 101) {
            st = BuildResult::TimedOut;
        }

        else if (hook && (!exited || exitCode != 100))
        {
        }

        else
        {
            assert(derivationType);
            st = dynamic_cast<NotDeterministic *>(&e)        ? BuildResult::NotDeterministic
                : exited && exitCode == 0                    ? BuildResult::OutputRejected
                : !derivationType->isSandboxed() || diskFull ? BuildResult::TransientFailure
                                                             : BuildResult::PermanentFailure;
        }

        co_return done(st, {}, std::move(e));
    }
} catch (...) {
    co_return result::current_exception();
}

namespace {
struct BuildHookLogger final : rpc::build_remote::HookInstance::BuildLogger::Server
{
    AutoCloseFD fd;

    BuildHookLogger(AutoCloseFD fd) : fd(std::move(fd)) {}

    kj::Maybe<int> getFd() override
    {
        return fd.get();
    }
};
}

kj::Promise<Result<HookResult>> DerivationGoal::tryBuildHook()
try {
    if (!worker.hook.available || !useDerivation) {
        co_return HookResult::Decline{};
    }

    // make sure we don't launch an unbounded number of build hooks
    auto hookSlot = co_await worker.hook.instancesSem.acquire();

    if (!worker.hook.instances.empty()) {
        hook = std::move(worker.hook.instances.front());
        worker.hook.instances.pop_front();
    } else {
        hook = TRY_AWAIT(HookInstance::create());
    }

    // open a pipe to receive logs directly from the hook
    Pipe logPipe;
    logPipe.create();
    builderOutFD = &logPipe.readSide;
    KJ_DEFER(builderOutFD = nullptr);

    KJ_DEFER(hook = nullptr);
    auto output = handleChildOutput();

    auto buildReq = hook->rpc.buildRequest();
    RPC_FILL(buildReq, setAmWilling, slotToken.valid());
    RPC_FILL(buildReq, setNeededSystem, drv->platform);
    RPC_FILL(buildReq, initDrvPath, drvPath, worker.store);
    RPC_FILL(buildReq, initRequiredFeatures, parsedDrv->getRequiredSystemFeatures());
    buildReq.setBuildLogger(kj::heap<BuildHookLogger>(std::move(logPipe.writeSide)));
    auto buildRespPromise = buildReq.send();
    auto buildResp = TRY_AWAIT_RPC(buildRespPromise);

    debug("hook reply is '%1%'", buildResp.toString().flatten().cStr());

    if (buildResp.isDecline()) {
        worker.hook.instances.push_back(std::move(hook));
        co_return HookResult::Decline{};
    } else if (buildResp.isDeclinePermanently()) {
        worker.hook.available = false;
        co_return HookResult::Decline{};
    } else if (buildResp.isPostpone()) {
        worker.hook.instances.push_back(std::move(hook));
        co_return HookResult::Postpone{};
    } else if (!buildResp.isAccept()) {
        throw Error("bad hook reply '%s'", buildResp.which());
    }

    // the build was accepted by the hook, we can free the slot for another build now
    hookSlot = {};

    machineName = rpc::to<std::string>(buildResp.getAccept().getMachineName());

    auto runReq = buildResp.getAccept().getMachine().runRequest();
    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    RPC_FILL(runReq, initInputs, inputPaths, worker.store);

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    {
        StringSet missingOutputs;
        for (auto & [outputName, status] : initialOutputs) {
            // XXX: Does this include known CA outputs?
            if (buildMode != bmCheck && status.known && status.known->isValid()) continue;
            missingOutputs.insert(outputName);
        }
        RPC_FILL(runReq, initWantedOutputs, missingOutputs);
    }

    /* Create the log file and pipe. */
    openLogFile();

    auto runPromise = runReq.send();

    // build via hook is now properly running. wait for it to finish
    actLock.reset();
    buildResult.startTime = time(0); // inexact
    started();

    auto result = co_await runPromise;

    // close the rpc connection to have the hook exit
    hook->rpc = nullptr;
    hook->client = std::nullopt;
    hook->conn = nullptr;

    if (auto error = TRY_AWAIT(output)) {
        co_return HookResult::Accept{std::move(*error)};
    }

    std::shared_ptr<Error> remoteError;
    if (result.getResult().isBad()) {
        remoteError = std::make_shared<Error>(from(result.getResult().getBad()));
        logErrorInfo(remoteError->info().level, remoteError->info());
    }
    co_return HookResult::Accept{TRY_AWAIT(buildDone(remoteError))};
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<SingleDrvOutputs>> DerivationGoal::registerOutputs()
{
    /* When using a build hook, the build hook can register the output
       as valid (by doing `nix-store --import').  If so we don't have
       to do anything here.

       We can only early return when the outputs are known a priori. For
       floating content-addressed derivations this isn't the case.
     */
    return assertPathValidity();
}

Path DerivationGoal::openLogFile()
{
    logSize = 0;

    if (!settings.keepLog) return "";

    auto baseName = std::string(baseNameOf(worker.store.printStorePath(drvPath)));

    /* Create a log file. */
    Path logDir;
    if (auto localStore = dynamic_cast<LocalStore *>(&worker.store))
        logDir = localStore->config().logDir;
    else
        logDir = settings.nixLogDir;
    Path dir = fmt("%s/%s/%s/", logDir, LocalFSStore::drvsLogDir, baseName.substr(0, 2));
    createDirs(dir);

    Path logFileName = fmt("%s/%s%s", dir, baseName.substr(2),
        settings.compressLog ? ".bz2" : "");

    fdLogFile = AutoCloseFD{open(logFileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0666)};
    if (!fdLogFile) throw SysError("creating log file '%1%'", logFileName);

    logFileSink = std::make_shared<FdSink>(fdLogFile.get());

    if (settings.compressLog)
        logSink = std::shared_ptr<CompressionSink>(makeCompressionSink("bzip2", *logFileSink));
    else
        logSink = logFileSink;

    return logFileName;
}


void DerivationGoal::closeLogFile()
{
    auto logSink2 = std::dynamic_pointer_cast<CompressionSink>(logSink);
    if (logSink2) logSink2->finish();
    if (logFileSink) logFileSink->flush();
    logSink = logFileSink = 0;
    fdLogFile.reset();
}


Goal::WorkResult DerivationGoal::tooMuchLogs()
{
    killChild();
    return done(
        BuildResult::LogLimitExceeded, {},
        Error("%s killed after writing more than %d bytes of log output",
            getName(), settings.maxLogSize));
}

kj::Promise<Result<std::optional<Goal::WorkResult>>>
DerivationGoal::handleBuilderOutput(AsyncInputStream & in) noexcept
try {
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
            co_return std::nullopt;
        }

        logSize += data.size();
        if (settings.maxLogSize && logSize > settings.maxLogSize) {
            co_return tooMuchLogs();
        }

        for (auto c : data)
            if (c == '\r')
                currentLogLinePos = 0;
            else if (c == '\n')
                flushLine();
            else {
                if (currentLogLinePos >= currentLogLine.size())
                    currentLogLine.resize(currentLogLinePos + 1);
                currentLogLine[currentLogLinePos++] = c;
            }

        if (logSink) (*logSink)(data);
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<Goal::WorkResult>>>
DerivationGoal::handleHookOutput(AsyncInputStream & in) noexcept
try {
    auto buf = kj::heapArray<char>(4096);
    while (true) {
        const auto got = TRY_AWAIT(in.read(buf.begin(), buf.size()));
        if (!got) {
            co_return std::nullopt;
        }

        std::string_view data = {buf.begin(), *got};
        lastChildActivity = AIO().provider.getTimer().now();

        for (auto c : data)
            if (c == '\n') {
                auto json = parseJSONMessage(currentHookLine, "the derivation builder");
                if (json) {
                    auto s = handleJSONLogMessage(*json, worker.act, hook->activities, "the derivation builder", true);
                    // ensure that logs from a builder using `ssh-ng://` as protocol
                    // are also available to `nix log`.
                    if (s && logSink) {
                        const auto type = (*json)["type"];
                        const auto fields = (*json)["fields"];
                        if (type == resBuildLogLine) {
                            const std::string logLine =
                                (fields.size() > 0 ? fields[0].get<std::string>() : "") + "\n";
                            logSize += logLine.size();
                            if (settings.maxLogSize && logSize > settings.maxLogSize) {
                                co_return tooMuchLogs();
                            }
                            (*logSink)(logLine);
                        } else if (type == resSetPhase && ! fields.is_null()) {
                            const auto phase = fields[0];
                            if (! phase.is_null()) {
                                // nixpkgs' stdenv produces lines in the log to signal
                                // phase changes.
                                // We want to get the same lines in case of remote builds.
                                // The format is:
                                //   @nix { "action": "setPhase", "phase": "$curPhase" }
                                const auto logLine = JSON::object({
                                    {"action", "setPhase"},
                                    {"phase", phase}
                                });
                                (*logSink)("@nix " + logLine.dump(-1, ' ', false, JSON::error_handler_t::replace) + "\n");
                            }
                        }
                    }
                }
                currentHookLine.clear();
            } else
                currentHookLine += c;
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<Goal::WorkResult>>> DerivationGoal::handleChildOutput() noexcept
try {
    kj::Own<AsyncInputStream> builderIn, hookIn;
    if (builderOutFD) {
        builderIn = kj::heap<AsyncFdIoStream>(AsyncFdIoStream::shared_fd{}, builderOutFD->get());
    }
    if (hook) {
        hookIn = kj::heap<AsyncFdIoStream>(AsyncFdIoStream::shared_fd{}, hook->fromHook.get());
    }

    auto handlers = handleChildStreams(builderIn.get(), hookIn.get())
                        .attach(std::move(builderIn), std::move(hookIn));

    if (respectsTimeouts() && settings.buildTimeout != 0) {
        handlers = handlers.exclusiveJoin(
            AIO()
                .provider.getTimer()
                .afterDelay(settings.buildTimeout.get() * kj::SECONDS)
                .then([this]() -> Result<std::optional<WorkResult>> {
                    return timedOut(
                        Error("%1% timed out after %2% seconds", name, settings.buildTimeout)
                    );
                })
        );
    }

    return handlers.then([this](auto r) {
        if (!currentLogLine.empty()) flushLine();
        return r;
    });
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<std::optional<Goal::WorkResult>>> DerivationGoal::monitorForSilence() noexcept
{
    while (true) {
        const auto stash = lastChildActivity;
        auto waitUntil = lastChildActivity + settings.maxSilentTime.get() * kj::SECONDS;
        co_await AIO().provider.getTimer().atTime(waitUntil);
        if (lastChildActivity == stash) {
            co_return timedOut(
                Error("%1% timed out after %2% seconds of silence", name, settings.maxSilentTime)
            );
        }
    }
}

kj::Promise<Result<std::optional<Goal::WorkResult>>>
DerivationGoal::handleChildStreams(AsyncInputStream * builderIn, AsyncInputStream * hookIn) noexcept
{
    assert(builderIn || hookIn);

    lastChildActivity = AIO().provider.getTimer().now();

    auto handlers = kj::joinPromisesFailFast([&] {
        kj::Vector<kj::Promise<Result<std::optional<WorkResult>>>> parts{2};

        if (builderIn) {
            parts.add(handleBuilderOutput(*builderIn));
        }
        if (hookIn) {
            parts.add(handleHookOutput(*hookIn));
        }

        return parts.releaseAsArray();
    }());

    if (respectsTimeouts() && settings.maxSilentTime != 0) {
        handlers = handlers.exclusiveJoin(monitorForSilence().then([](auto r) {
            return kj::arr(std::move(r));
        }));
    }

    for (auto r : co_await handlers) {
        if (r) {
            co_return r;
        }
    }
    co_return std::nullopt;
}

void DerivationGoal::flushLine()
{
    if (handleJSONLogMessage(currentLogLine, *act, builderActivities, "the derivation builder", false))
        ;

    else {
        logTail.push_back(currentLogLine);
        if (logTail.size() > settings.logLines) logTail.pop_front();

        act->result(resBuildLogLine, currentLogLine);
    }

    currentLogLine = "";
    currentLogLinePos = 0;
}


kj::Promise<Result<OutputPathMap>> DerivationGoal::queryDerivationOutputMap()
try {
    OutputPathMap res;
    for (auto & [name, output] : drv->outputsAndPaths(worker.store))
        res.insert_or_assign(name, output.second);
    co_return res;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::pair<bool, SingleDrvOutputs>>> DerivationGoal::checkPathValidity()
try {
    bool checkHash = buildMode == bmRepair;
    auto wantedOutputsLeft = std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            return StringSet {};
        },
        [&](const OutputsSpec::Names & names) {
            return static_cast<StringSet>(names);
        },
    }, wantedOutputs.raw);
    SingleDrvOutputs validOutputs;

    for (auto & i : TRY_AWAIT(queryDerivationOutputMap())) {
        auto initialOutput = get(initialOutputs, i.first);
        if (!initialOutput)
            // this is an invalid output, gets catched with (!wantedOutputsLeft.empty())
            continue;
        auto & info = *initialOutput;
        info.wanted = wantedOutputs.contains(i.first);
        if (info.wanted)
            wantedOutputsLeft.erase(i.first);
        auto & outputPath = i.second;
        info.known = {
            .path = outputPath,
            .status = !TRY_AWAIT(worker.store.isValidPath(outputPath))
                ? PathStatus::Absent
                : !checkHash || TRY_AWAIT(worker.pathContentsGood(outputPath))
                ? PathStatus::Valid
                : PathStatus::Corrupt,
        };
        auto drvOutput = DrvOutput{info.outputHash, i.first};
        if (info.known && info.known->isValid())
            validOutputs.emplace(i.first, Realisation { drvOutput, info.known->path });
    }

    // If we requested all the outputs, we are always fine.
    // If we requested specific elements, the loop above removes all the valid
    // ones, so any that are left must be invalid.
    if (!wantedOutputsLeft.empty())
        throw Error("derivation '%s' does not have wanted outputs %s",
            worker.store.printStorePath(drvPath),
            concatStringsSep(", ", quoteStrings(wantedOutputsLeft)));

    bool allValid = needRestart != NeedRestartForMoreOutputs::OutputsAddedDoNeed;
    for (auto & [_, status] : initialOutputs) {
        if (!status.wanted) continue;
        if (!status.known || !status.known->isValid()) {
            allValid = false;
            break;
        }
    }

    co_return { allValid, validOutputs };
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<SingleDrvOutputs>> DerivationGoal::assertPathValidity()
try {
    auto [allValid, validOutputs] = TRY_AWAIT(checkPathValidity());
    if (!allValid)
        throw Error("some outputs are unexpectedly invalid");
    co_return validOutputs;
} catch (...) {
    co_return result::current_exception();
}


Goal::WorkResult DerivationGoal::done(
    BuildResult::Status status,
    SingleDrvOutputs builtOutputs,
    std::optional<Error> ex)
{
    isDone = true;

    outputLocks.reset();
    buildResult.status = status;
    if (ex)
        buildResult.errorMsg = fmt("%s", Uncolored(ex->info().msg));

    mcExpectedBuilds.reset();
    mcRunningBuilds.reset();

    if (buildResult.success()) {
        auto wantedBuiltOutputs = filterDrvOutputs(wantedOutputs, std::move(builtOutputs));
        assert(!wantedBuiltOutputs.empty());
        buildResult.builtOutputs = std::move(wantedBuiltOutputs);
        if (status == BuildResult::Built)
            worker.doneBuilds++;
    } else {
        if (status != BuildResult::DependencyFailed)
            worker.failedBuilds++;
    }

    auto traceBuiltOutputsFile = getEnv("_NIX_TRACE_BUILT_OUTPUTS").value_or("");
    if (traceBuiltOutputsFile != "") {
        std::fstream fs;
        fs.open(traceBuiltOutputsFile, std::fstream::out);
        fs << worker.store.printStorePath(drvPath) << "\t" << buildResult.toString() << std::endl;
    }

    if (ex && isDependency) {
        logError(ex->info());
    }

    return WorkResult{
        .exitCode = buildResult.success() ? ecSuccess : ecFailed,
        .result = buildResult,
        .ex = ex ? std::make_shared<Error>(std::move(*ex)) : nullptr,
        .permanentFailure = buildResult.status == BuildResult::PermanentFailure,
        .timedOut = buildResult.status == BuildResult::TimedOut,
        .hashMismatch = anyHashMismatchSeen,
        .checkMismatch = anyCheckMismatchSeen,
    };
}


void DerivationGoal::waiteeDone(GoalPtr waitee)
{
    if (!useDerivation) return;

    auto * dg = dynamic_cast<DerivationGoal *>(&*waitee);
    if (!dg) return;

    auto & fullDrv = *dynamic_cast<Derivation *>(drv.get());

    auto * nodeP = get(fullDrv.inputDrvs, dg->drvPath);
    if (!nodeP) return;
    auto & outputs = *nodeP;

    for (auto & outputName : outputs) {
        auto buildResult = dg->buildResult.restrictTo(DerivedPath::Built {
            .drvPath = makeConstantStorePath(dg->drvPath),
            .outputs = OutputsSpec::Names { outputName },
        });
        if (buildResult.success()) {
            auto i = buildResult.builtOutputs.find(outputName);
            if (i != buildResult.builtOutputs.end())
                inputDrvOutputs.insert_or_assign(
                    { dg->drvPath, outputName },
                    i->second.outPath);
        }
    }
}
}
