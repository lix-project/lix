#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/c-calls.hh"
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
#include <cstdint>
#include <exception>
#include <fstream>
#include <kj/array.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/time.h>
#include <kj/vector.h>
#include <limits>
#include <memory>
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
}


Goal::WorkResult DerivationGoal::timedOut(Error && ex)
{
    killChild();
    return done(BuildResult::TimedOut, {}, std::move(ex));
}


kj::Promise<Result<Goal::WorkResult>> DerivationGoal::workImpl() noexcept
{
    // always clear the slot token, no matter what happens. not doing this
    // can cause builds to get stuck on exceptions (or other early exits).
    // ideally we'd use scoped slot tokens instead of keeping them in some
    // goal member variable, but we cannot do this yet for legacy reasons.
    KJ_DEFER({
        actLock.reset();
        slotToken = {};
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

    if (buildMode == bmCheck && validOutputs.empty()) {
        throw Error(
            "'%s' has no valid outputs registered in the store, build it first and re-run the "
            "check command after that",
            worker.store.printStorePath(drvPath)
        );
    } else if (buildMode == bmCheck && !allValid) {
        auto wantedOutputsStr = wantedOutputs.to_string();
        auto validOutputsNames = concatStringsSep(", ", std::views::keys(validOutputs));
        throw Error(
            "Not all outputs of '%s' are registered and valid in this store ('%s' are available, "
            "'%s' are missing). "
            "Rebuild the derivation normally and re-run the check command after that",
            worker.store.printStorePath(drvPath),
            validOutputsNames == "" ? "none" : validOutputsNames,
            wantedOutputsStr == "*" ? "all" : wantedOutputsStr
        );
    }

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

std::string DerivationGoal::buildDescription() const
{
    return fmt(
        buildMode == bmRepair      ? "repairing outputs of '%s'"
            : buildMode == bmCheck ? "checking outputs of '%s'"
                                   : "building '%s'",
        worker.store.printStorePath(drvPath)
    );
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
            actLock = logger->startActivity(
                lvlWarn,
                actBuildWaiting,
                fmt("waiting for lock on %s", Magenta(showPaths(lockFiles)))
            );
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
                actLock = logger->startActivity(
                    lvlTalkative,
                    actBuildWaiting,
                    fmt("waiting for a machine to build '%s'",
                        Magenta(worker.store.printStorePath(drvPath)))
                );
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
        oldPath = makeTempSiblingPath(storePath);
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
    return hook->kill();
}

void DerivationGoal::closeReadPipes() {}

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

    auto act = logger.startActivity(
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
                ACTIVITY_RESULT(act, resPostBuildLogLine, currentLine);
                currentLine.clear();
            } else {
                currentLine += c;
            }
        }
    }

    if (currentLine != "") {
        currentLine += '\n';
        ACTIVITY_RESULT(act, resPostBuildLogLine, currentLine);
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
struct ActivityTrackingHookLogger final : HookInstance::HookLogger
{
    kj::TimePoint & tracker;

    ActivityTrackingHookLogger(const Activity & act, FinishSink * logSink, kj::TimePoint & tracker)
        : HookLogger(act, logSink)
        , tracker(tracker)
    {
    }

    kj::Promise<void> push(PushContext context) override
    {
        tracker = AIO().provider.getTimer().now();
        return HookLogger::push(context);
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
        hook = TRY_AWAIT(HookInstance::create(worker.act));
    }

    KJ_DEFER(hook = nullptr);

    auto buildReq = hook->rpc->buildRequest();
    RPC_FILL(buildReq, setAmWilling, slotToken.valid());
    RPC_FILL(buildReq, setNeededSystem, drv->platform);
    RPC_FILL(buildReq, initDrvPath, drvPath, worker.store);
    RPC_FILL(buildReq, initRequiredFeatures, parsedDrv->getRequiredSystemFeatures());
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

    /* Create the log file and pipe. */
    openLogFile();

    auto runReq = buildResp.getAccept().getMachine().runRequest();
    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    runReq.setLogger(
        kj::heap<ActivityTrackingHookLogger>(worker.act, logSink.get(), lastChildActivity)
    );
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
        RPC_FILL(runReq, setDescription, buildDescription());
    }

    auto runPromise = runReq.send();

    // build via hook is now properly running. wait for it to finish
    actLock.reset();
    buildResult.startTime = time(0); // inexact
    mcRunningBuilds = worker.runningBuilds.addTemporarily(1);

    auto result = TRY_AWAIT(
        wrapChildHandler(runPromise.then([&](auto result) -> kj::Promise<Result<WorkResult>> {
            try {
                std::shared_ptr<Error> remoteError;
                if (result.getResult().isBad()) {
                    remoteError = std::make_shared<Error>(from(result.getResult().getBad()));
                    logErrorInfo(remoteError->info().level, remoteError->info());
                }
                // close the rpc connection to have the hook exit
                hook->rpc = nullptr;
                hook->wait();
                return buildDone(remoteError);
            } catch (...) {
                return {result::current_exception()};
            }
        }))
    );

    co_return HookResult::Accept{std::move(result)};
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

DerivationGoal::LogSink::LogSink(AutoCloseFD fd, ref<BufferedSink> file, bool compress, uint64_t limit)
    : fd(std::move(fd))
    , file(file)
    , target(compress ? makeCompressionSink("bzip2", *file) : file)
    , limit(limit)
{
}

DerivationGoal::LogSink::~LogSink() noexcept
{
    signal.fulfiller->fulfill(false);
}

void DerivationGoal::LogSink::operator()(std::string_view data)
{
    writtenSoFar += data.size();
    if (writtenSoFar <= limit) {
        (*target)(data);
    } else {
        signal.fulfiller->fulfill(true);
    }
}

void DerivationGoal::LogSink::finish()
{
    if (auto inner2 = dynamic_cast<FinishSink *>(&*target)) {
        inner2->finish();
    }
    file->flush();
}

Path DerivationGoal::openLogFile()
{
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

    auto fdLogFile = sys::open(logFileName, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
    if (!fdLogFile) throw SysError("creating log file '%1%'", logFileName);

    auto logFileSink = make_ref<FdSink>(fdLogFile.get());
    const auto logLimit =
        settings.maxLogSize ? settings.maxLogSize.get() : std::numeric_limits<uint64_t>::max();

    logSink = std::make_unique<LogSink>(
        std::move(fdLogFile), logFileSink, settings.compressLog, logLimit
    );

    return logFileName;
}

void DerivationGoal::closeLogFile()
{
    if (logSink) {
        logSink->finish();
    }
    logSink = 0;
}


Goal::WorkResult DerivationGoal::tooMuchLogs()
{
    killChild();
    return done(
        BuildResult::LogLimitExceeded, {},
        Error("%s killed after writing more than %d bytes of log output",
            getName(), settings.maxLogSize));
}

kj::Promise<Result<Goal::WorkResult>>
DerivationGoal::wrapChildHandler(kj::Promise<Result<WorkResult>> handler) noexcept
{
    if (respectsTimeouts() && settings.maxSilentTime != 0) {
        handler = handler.exclusiveJoin(monitorForSilence());
    }

    if (respectsTimeouts() && settings.buildTimeout != 0) {
        handler = handler.exclusiveJoin(
            AIO()
                .provider.getTimer()
                .afterDelay(settings.buildTimeout.get() * kj::SECONDS)
                .then([this]() -> Result<WorkResult> {
                    return timedOut(
                        Error("%1% timed out after %2% seconds", name, settings.buildTimeout)
                    );
                })
        );
    }

    if (logSink) {
        handler = handler.exclusiveJoin(
            logSink->signal.promise.then([&](bool limitReached) -> kj::Promise<Result<WorkResult>> {
                try {
                    if (limitReached) {
                        return {tooMuchLogs()};
                    } else {
                        return kj::NEVER_DONE;
                    }
                } catch (...) {
                    return {result::current_exception()};
                }
            })
        );
    }

    return handler;
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::monitorForSilence() noexcept
{
    lastChildActivity = AIO().provider.getTimer().now();

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
