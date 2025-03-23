#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/build/hook-instance.hh"
#include "lix/libstore/build/worker.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/json.hh"
#include "lix/libstore/common-protocol.hh"
#include "lix/libstore/common-protocol-impl.hh" // IWYU pragma: keep
#include "lix/libstore/local-store.hh" // TODO remove, along with remaining downcasts
#include "lix/libutil/logging-json.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libstore/build/drv-output-substitution-goal.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"

#include <boost/outcome/try.hpp>
#include <fstream>
#include <kj/array.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/debug.h>
#include <kj/vector.h>
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
        DerivedPath::Built { makeConstantStorePathRef(drvPath), wantedOutputs }.to_string(worker.store));
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
        DerivedPath::Built { makeConstantStorePathRef(drvPath), drv.outputNames() }.to_string(worker.store));
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

         - Dynamic derivations are built, and so are found in the main store.
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

    if (!drv->type().hasKnownOutputPaths())
        experimentalFeatureSettings.require(Xp::CaDerivations);

    if (!drv->type().isPure()) {
        experimentalFeatureSettings.require(Xp::ImpureDerivations);

        for (auto & [outputName, output] : drv->outputs) {
            auto randomPath = StorePath::random(outputPathName(drv->name, outputName));
            assert(!TRY_AWAIT(worker.store.isValidPath(randomPath)));
            initialOutputs.insert({
                outputName,
                InitialOutput {
                    .wanted = true,
                    .outputHash = impureOutputHash,
                    .known = InitialOutputStatus {
                        .path = randomPath,
                        .status = PathStatus::Absent
                    }
                }
            });
        }

        co_return co_await gaveUpOnSubstitution();
    }

    for (auto & i : drv->outputsAndOptPaths(worker.store))
        if (i.second.second)
            TRY_AWAIT(worker.store.addTempRoot(*i.second.second));

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
                if (!status.known)
                    dependencies.add(
                        worker.goalFactory().makeDrvOutputSubstitutionGoal(
                            DrvOutput{status.outputHash, outputName},
                            buildMode == bmRepair ? Repair : NoRepair
                        )
                    );
                else {
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

    assert(drv->type().isPure());

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
        std::function<void(ref<SingleDerivedPath>, const DerivedPathMap<StringSet>::ChildNode &)> addWaiteeDerivedPath;

        addWaiteeDerivedPath = [&](ref<SingleDerivedPath> inputDrv, const DerivedPathMap<StringSet>::ChildNode & inputNode) {
            if (!inputNode.value.empty())
                dependencies.add(worker.goalFactory().makeGoal(
                    DerivedPath::Built {
                        .drvPath = inputDrv,
                        .outputs = inputNode.value,
                    },
                    buildMode == bmRepair ? bmRepair : bmNormal));
            for (const auto & [outputName, childNode] : inputNode.childMap)
                addWaiteeDerivedPath(
                    make_ref<SingleDerivedPath>(SingleDerivedPath::Built { inputDrv, outputName }),
                    childNode);
        };

        for (const auto & [inputDrvPath, inputNode] : dynamic_cast<Derivation *>(drv.get())->inputDrvs.map) {
            /* Ensure that pure, non-fixed-output derivations don't
               depend on impure derivations. */
            if (experimentalFeatureSettings.isEnabled(Xp::ImpureDerivations) && drv->type().isPure() && !drv->type().isFixed()) {
                auto inputDrv = TRY_AWAIT(worker.evalStore.readDerivation(inputDrvPath));
                if (!inputDrv.type().isPure())
                    throw Error("pure derivation '%s' depends on impure derivation '%s'",
                        worker.store.printStorePath(drvPath),
                        worker.store.printStorePath(inputDrvPath));
            }

            addWaiteeDerivedPath(makeConstantStorePathRef(inputDrvPath), inputNode);
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
    assert(drv->type().isPure());

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
                TRY_AWAIT(worker.store.queryPartialDerivationOutputMap(i, &worker.evalStore));
            for (auto & j : depOutputs)
                if (j.second)
                    outputsToDrv.insert_or_assign(*j.second, i);
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
                    .drvPath = makeConstantStorePathRef(drvPath2->second),
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

        auto drvType = fullDrv.type();
        bool resolveDrv = std::visit(overloaded {
            [&](const DerivationType::InputAddressed & ia) {
                /* must resolve if deferred. */
                return ia.deferred;
            },
            [&](const DerivationType::ContentAddressed & ca) {
                return !fullDrv.inputDrvs.map.empty() && (
                    ca.fixed
                    /* Can optionally resolve if fixed, which is good
                       for avoiding unnecessary rebuilds. */
                    ? experimentalFeatureSettings.isEnabled(Xp::CaDerivations)
                    /* Must resolve if floating and there are any inputs
                       drvs. */
                    : true);
            },
            [&](const DerivationType::Impure &) {
                return true;
            }
        }, drvType.raw);

        if (resolveDrv && !fullDrv.inputDrvs.map.empty()) {
            experimentalFeatureSettings.require(Xp::CaDerivations);

            /* We are be able to resolve this derivation based on the
               now-known results of dependencies. If so, we become a
               stub goal aliasing that resolved derivation goal. */
            std::optional attempt = TRY_AWAIT(fullDrv.tryResolve(worker.store, inputDrvOutputs));
            if (!attempt) {
              /* TODO (impure derivations-induced tech debt) (see below):
                 The above attempt should have found it, but because we manage
                 inputDrvOutputs statefully, sometimes it gets out of sync with
                 the real source of truth (store). So we query the store
                 directly if there's a problem. */
              attempt = TRY_AWAIT(fullDrv.tryResolve(worker.store, &worker.evalStore));
            }
            assert(attempt);
            Derivation drvResolved { std::move(*attempt) };

            auto pathResolved = TRY_AWAIT(writeDerivation(worker.store, drvResolved));

            auto msg = fmt("resolved derivation: '%s' -> '%s'",
                worker.store.printStorePath(drvPath),
                worker.store.printStorePath(pathResolved));
            act = std::make_unique<Activity>(*logger, lvlInfo, actBuildWaiting, msg,
                Logger::Fields {
                       worker.store.printStorePath(drvPath),
                       worker.store.printStorePath(pathResolved),
                   });

            auto dependency = worker.goalFactory().makeDerivationGoal(
                pathResolved, wantedOutputs, buildMode);
            resolvedDrvGoal = dependency.first;

            TRY_AWAIT(waitForGoals(std::move(dependency)));
            co_return co_await resolvedFinished();
        }

        std::function<kj::Promise<Result<void>>(const StorePath &, const DerivedPathMap<StringSet>::ChildNode &)> accumInputPaths;

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        accumInputPaths = [&](const StorePath & depDrvPath, const DerivedPathMap<StringSet>::ChildNode & inputNode) -> kj::Promise<Result<void>> {
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

                for (auto & outputName : inputNode.value) {
                    TRY_AWAIT(
                        worker.store.computeFSClosure(TRY_AWAIT(getOutput(outputName)), inputPaths)
                    );
                }

                for (auto & [outputName, childNode] : inputNode.childMap)
                    TRY_AWAIT(accumInputPaths(TRY_AWAIT(getOutput(outputName)), childNode));
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        };

        for (auto & [depDrvPath, depNode] : fullDrv.inputDrvs.map)
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
    /* FIXME: Should lock something like the drv itself so we don't build same
       CA drv concurrently */
    if (dynamic_cast<LocalStore *>(&worker.store)) {
        /* If we aren't a local store, we might need to use the local store as
           a build remote, but that would cause a deadlock. */
        /* FIXME: Make it so we can use ourselves as a build remote even if we
           are the local store (separate locking for building vs scheduling? */
        /* FIXME: find some way to lock for scheduling for the other stores so
           a forking daemon with --store still won't farm out redundant builds.
           */
        for (auto & i : drv->outputsAndOptPaths(worker.store)) {
            if (i.second.second)
                lockFiles.insert(worker.store.Store::toRealPath(*i.second.second));
            else
                lockFiles.insert(
                    worker.store.Store::toRealPath(drvPath) + "." + i.first
                );
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
        auto hookReply = tryBuildHook();
        switch (hookReply.index()) {
        case 0: {
            HookReply::Accept & a = std::get<0>(hookReply);
            /* Yes, it has started doing so.  Wait until we get
                EOF from the hook. */
            actLock.reset();
            buildResult.startTime = time(0); // inexact
            started();
            auto r = co_await a.promise;
            if (r.has_value()) {
                co_return co_await buildDone();
            } else if (r.has_error()) {
                co_return r.assume_error();
            } else {
                co_return r.assume_exception();
            }
        }

        case 1: {
            HookReply::Decline _ [[gnu::unused]] = std::get<1>(hookReply);
            break;
        }

        case 2: {
            HookReply::Postpone _ [[gnu::unused]] = std::get<2>(hookReply);
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
    hook->builderOut.reset();
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

void runPostBuildHook(
    Store & store,
    Logger & logger,
    const StorePath & drvPath,
    const StorePathSet & outputPaths)
{
    auto hook = settings.postBuildHook;
    if (hook == "")
        return;

    Activity act(logger, lvlTalkative, actPostBuildHook,
            fmt("running post-build-hook '%s'", settings.postBuildHook),
            Logger::Fields{store.printStorePath(drvPath)});
    PushActivity pact(act.id);
    std::map<std::string, std::string> hookEnvironment = getEnv();

    auto drvPathPretty = store.printStorePath(drvPath);
    hookEnvironment.emplace("DRV_PATH", drvPathPretty);
    hookEnvironment.emplace("OUT_PATHS", chomp(concatStringsSep(" ", store.printStorePathSet(outputPaths))));
    hookEnvironment.emplace("NIX_CONFIG", globalConfig.toKeyValue(true));

    struct LogSink : Sink {
        Activity & act;
        std::string currentLine;

        LogSink(Activity & act) : act(act) { }

        void operator() (std::string_view data) override {
            for (auto c : data) {
                if (c == '\n') {
                    flushLine();
                } else {
                    currentLine += c;
                }
            }
        }

        void flushLine() {
            act.result(resPostBuildLogLine, currentLine);
            currentLine.clear();
        }

        ~LogSink() {
            if (currentLine != "") {
                currentLine += '\n';
                flushLine();
            }
        }
    };
    LogSink sink(act);

    auto proc = runProgram2({
        .program = settings.postBuildHook,
        .environment = hookEnvironment,
        .captureStdout = true,
        .mergeStderrToStdout = true,
    });
    Finally const _wait([&] {
        try {
            proc.wait();
        } catch (nix::Error & e) {
            e.addTrace(nullptr,
                "while running the post-build-hook %s for derivation %s",
                settings.postBuildHook,
                drvPathPretty
            );
            throw;
        }
    });

    // FIXME just process the data, without a wrapper sink class
    proc.getStdout()->drainInto(sink);
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::buildDone() noexcept
try {
    trace("build done");

    slotToken = {};
    Finally releaseBuildUser([&](){ this->cleanupHookFinally(); });

    cleanupPreChildKill();

    /* Since we got an EOF on the logger pipe, the builder is presumed
       to have terminated.  In fact, the builder could also have
       simply have closed its end of the pipe, so just to be sure,
       kill it. */
    int status = getChildStatus();

    debug("builder process for '%s' finished", worker.store.printStorePath(drvPath));

    buildResult.timesBuilt++;
    buildResult.stopTime = time(0);

    /* Close the read side of the logger pipe. */
    closeReadPipes();

    /* Close the log file. */
    closeLogFile();

    cleanupPostChildKill();

    if (buildResult.cpuUser && buildResult.cpuSystem) {
        debug("builder for '%s' terminated with status %d, user CPU %.3fs, system CPU %.3fs",
            worker.store.printStorePath(drvPath),
            status,
            ((double) buildResult.cpuUser->count()) / 1000000,
            ((double) buildResult.cpuSystem->count()) / 1000000);
    }

    bool diskFull = false;

    try {

        /* Check the exit status. */
        if (!statusOk(status)) {

            diskFull |= cleanupDecideWhetherDiskFull();

            auto msg = fmt("builder for '%s' %s",
                Magenta(worker.store.printStorePath(drvPath)),
                statusToString(status));

            if (!logger->isVerbose() && !logTail.empty()) {
                msg += fmt(";\nlast %d log lines:\n", logTail.size());
                for (auto & line : logTail) {
                    msg += "> ";
                    msg += line;
                    msg += "\n";
                }
                auto nixLogCommand = experimentalFeatureSettings.isEnabled(Xp::NixCommand)
                    ? "nix log"
                    : "nix-store -l";
                msg += fmt("For full logs, run '" ANSI_BOLD "%s %s" ANSI_NORMAL "'.",
                    nixLogCommand,
                    worker.store.printStorePath(drvPath));
            }

            if (diskFull)
                msg += "\nnote: build failure may have been caused by lack of free disk space";

            throw BuildError("%s", msg);
        }

        /* Compute the FS closure of the outputs and register them as
           being valid. */
        auto builtOutputs = TRY_AWAIT(registerOutputs());

        StorePathSet outputPaths;
        for (auto & [_, output] : builtOutputs)
            outputPaths.insert(output.outPath);
        runPostBuildHook(
            worker.store,
            *logger,
            drvPath,
            outputPaths
        );

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

        if (hook && WIFEXITED(status) && WEXITSTATUS(status) == 101)
            st = BuildResult::TimedOut;

        else if (hook && (!WIFEXITED(status) || WEXITSTATUS(status) != 100)) {
        }

        else {
            assert(derivationType);
            st =
                dynamic_cast<NotDeterministic*>(&e) ? BuildResult::NotDeterministic :
                statusOk(status) ? BuildResult::OutputRejected :
                !derivationType->isSandboxed() || diskFull ? BuildResult::TransientFailure :
                BuildResult::PermanentFailure;
        }

        co_return done(st, {}, std::move(e));
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DerivationGoal::resolvedFinished() noexcept
try {
    trace("resolved derivation finished");

    assert(resolvedDrvGoal);
    auto resolvedDrv = *resolvedDrvGoal->drv;
    auto & resolvedResult = resolvedDrvGoal->buildResult;

    SingleDrvOutputs builtOutputs;

    if (resolvedResult.success()) {
        auto resolvedHashes = TRY_AWAIT(staticOutputHashes(worker.store, resolvedDrv));

        StorePathSet outputPaths;

        for (auto & outputName : resolvedDrv.outputNames()) {
            auto initialOutput = get(initialOutputs, outputName);
            auto resolvedHash = get(resolvedHashes, outputName);
            if ((!initialOutput) || (!resolvedHash))
                throw Error(
                    "derivation '%s' doesn't have expected output '%s' (derivation-goal.cc/resolvedFinished,resolve)",
                    worker.store.printStorePath(drvPath), outputName);

            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            auto realisation = TRY_AWAIT([&]() -> kj::Promise<Result<Realisation>> {
                try {
                    auto take1 = get(resolvedResult.builtOutputs, outputName);
                    if (take1) co_return *take1;

                    /* The above `get` should work. But sateful tracking of
                       outputs in resolvedResult, this can get out of sync with the
                       store, which is our actual source of truth. For now we just
                       check the store directly if it fails. */
                    auto take2 = TRY_AWAIT(
                        worker.evalStore.queryRealisation(DrvOutput{*resolvedHash, outputName})
                    );
                    if (take2) co_return *take2;

                    throw Error(
                        "derivation '%s' doesn't have expected output '%s' (derivation-goal.cc/resolvedFinished,realisation)",
                        worker.store.printStorePath(resolvedDrvGoal->drvPath), outputName);
                } catch (...) {
                    co_return result::current_exception();
                }
            }());

            if (drv->type().isPure()) {
                auto newRealisation = realisation;
                newRealisation.id = DrvOutput { initialOutput->outputHash, outputName };
                newRealisation.signatures.clear();
                if (!drv->type().isFixed()) {
                    auto & drvStore = TRY_AWAIT(worker.evalStore.isValidPath(drvPath))
                        ? worker.evalStore
                        : worker.store;
                    newRealisation.dependentRealisations = TRY_AWAIT(
                        drvOutputReferences(worker.store, *drv, realisation.outPath, &drvStore)
                    );
                }
                signRealisation(newRealisation);
                TRY_AWAIT(worker.store.registerDrvOutput(newRealisation));
            }
            outputPaths.insert(realisation.outPath);
            builtOutputs.emplace(outputName, realisation);
        }

        runPostBuildHook(
            worker.store,
            *logger,
            drvPath,
            outputPaths
        );
    }

    auto status = resolvedResult.status;
    if (status == BuildResult::AlreadyValid)
        status = BuildResult::ResolvesToAlreadyValid;

    co_return done(status, std::move(builtOutputs));
} catch (...) {
    co_return result::current_exception();
}

HookReply DerivationGoal::tryBuildHook()
{
    if (!worker.hook.available || !useDerivation) return HookReply::Decline{};

    if (!worker.hook.instance)
        worker.hook.instance = std::make_unique<HookInstance>();

    try {

        /* Send the request to the hook. */
        worker.hook.instance->sink
            << "try"
            << (slotToken.valid() ? 1 : 0)
            << drv->platform
            << worker.store.printStorePath(drvPath)
            << parsedDrv->getRequiredSystemFeatures();
        worker.hook.instance->sink.flush();

        /* Read the first line of input, which should be a word indicating
           whether the hook wishes to perform the build. */
        std::string reply;
        while (true) {
            auto s = [&]() {
                try {
                    return readLine(worker.hook.instance->fromHook.get());
                } catch (Error & e) {
                    e.addTrace({}, "while reading the response from the build hook");
                    throw;
                }
            }();
            if (handleJSONLogMessage(s, worker.act, worker.hook.instance->activities, "the build hook", true))
                ;
            else if (s.substr(0, 2) == "# ") {
                reply = s.substr(2);
                break;
            }
            else {
                s += "\n";
                writeLogsToStderr(s);
                logger->log(lvlInfo, s);
            }
        }

        debug("hook reply is '%1%'", reply);

        if (reply == "decline")
            return HookReply::Decline{};
        else if (reply == "decline-permanently") {
            worker.hook.available = false;
            worker.hook.instance.reset();
            return HookReply::Decline{};
        }
        else if (reply == "postpone")
            return HookReply::Postpone{};
        else if (reply != "accept")
            throw Error("bad hook reply '%s'", reply);

    } catch (SysError & e) {
        if (e.errNo == EPIPE) {
            printError(
                "build hook died unexpectedly: %s",
                chomp(drainFD(worker.hook.instance->fromHook.get())));
            worker.hook.instance.reset();
            return HookReply::Decline{};
        } else
            throw;
    }

    hook = std::move(worker.hook.instance);

    try {
        machineName = readLine(hook->fromHook.get());
    } catch (Error & e) {
        e.addTrace({}, "while reading the machine name from the build hook");
        throw;
    }

    /* Tell the hook all the inputs that have to be copied to the
       remote system. */
    hook->sink << CommonProto::write(worker.store, {}, inputPaths);

    /* Tell the hooks the missing outputs that have to be copied back
       from the remote system. */
    {
        StringSet missingOutputs;
        for (auto & [outputName, status] : initialOutputs) {
            // XXX: Does this include known CA outputs?
            if (buildMode != bmCheck && status.known && status.known->isValid()) continue;
            missingOutputs.insert(outputName);
        }
        hook->sink << CommonProto::write(worker.store, {}, missingOutputs);
    }

    hook->sink = FdSink();
    hook->toHook.reset();

    /* Create the log file and pipe. */
    openLogFile();

    builderOutFD = &hook->builderOut;
    return HookReply::Accept{handleChildOutput()};
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

struct DerivationGoal::InputStream final : private kj::AsyncObject
{
    int fd;
    kj::UnixEventPort::FdObserver observer;

    InputStream(kj::UnixEventPort & ep, int fd)
        : fd(fd)
        , observer(ep, fd, kj::UnixEventPort::FdObserver::OBSERVE_READ)
    {
        int flags = fcntl(fd, F_GETFL);
        if (flags < 0) {
            throw SysError("fcntl(F_GETFL) failed on fd %i", fd);
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            throw SysError("fcntl(F_SETFL) failed on fd %i", fd);
        }
    }

    kj::Promise<std::string_view> read(kj::ArrayPtr<char> buffer)
    {
        const auto res = ::read(fd, buffer.begin(), buffer.size());
        // closing a pty endpoint causes EIO on the other endpoint. stock kj streams
        // do not handle this and throw exceptions we can't ask for errno instead :(
        // (we can't use `errno` either because kj may well have mangled it by now.)
        if (res == 0 || (res == -1 && errno == EIO)) {
            return std::string_view{};
        }

        KJ_NONBLOCKING_SYSCALL(res) {}

        if (res > 0) {
            return std::string_view{buffer.begin(), static_cast<size_t>(res)};
        }

        return observer.whenBecomesReadable().then([this, buffer] {
            return read(buffer);
        });
    }
};

kj::Promise<Outcome<void, Goal::WorkResult>> DerivationGoal::handleBuilderOutput(InputStream & in) noexcept
try {
    auto buf = kj::heapArray<char>(4096);
    while (true) {
        auto data = co_await in.read(buf);
        lastChildActivity = AIO().provider.getTimer().now();

        if (data.empty()) {
            co_return result::success();
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

kj::Promise<Outcome<void, Goal::WorkResult>> DerivationGoal::handleHookOutput(InputStream & in) noexcept
try {
    auto buf = kj::heapArray<char>(4096);
    while (true) {
        auto data = co_await in.read(buf);
        lastChildActivity = AIO().provider.getTimer().now();

        if (data.empty()) {
            co_return result::success();
        }

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

kj::Promise<Outcome<void, Goal::WorkResult>> DerivationGoal::handleChildOutput() noexcept
try {
    assert(builderOutFD);

    auto builderIn = kj::heap<InputStream>(AIO().unixEventPort, builderOutFD->get());
    kj::Own<InputStream> hookIn;
    if (hook) {
        hookIn = kj::heap<InputStream>(AIO().unixEventPort, hook->fromHook.get());
    }

    auto handlers = handleChildStreams(*builderIn, hookIn.get()).attach(std::move(builderIn), std::move(hookIn));

    if (respectsTimeouts() && settings.buildTimeout != 0) {
        handlers = handlers.exclusiveJoin(
            AIO().provider.getTimer()
                .afterDelay(settings.buildTimeout.get() * kj::SECONDS)
                .then([this]() -> Outcome<void, WorkResult> {
                    return timedOut(
                        Error("%1% timed out after %2% seconds", name, settings.buildTimeout)
                    );
                })
        );
    }

    return handlers.then([this](auto r) -> Outcome<void, WorkResult> {
        if (!currentLogLine.empty()) flushLine();
        return r;
    });
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Outcome<void, Goal::WorkResult>> DerivationGoal::monitorForSilence() noexcept
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

kj::Promise<Outcome<void, Goal::WorkResult>>
DerivationGoal::handleChildStreams(InputStream & builderIn, InputStream * hookIn) noexcept
{
    lastChildActivity = AIO().provider.getTimer().now();

    auto handlers = kj::joinPromisesFailFast([&] {
        kj::Vector<kj::Promise<Outcome<void, WorkResult>>> parts{2};

        parts.add(handleBuilderOutput(builderIn));
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
        BOOST_OUTCOME_CO_TRYV(r);
    }
    co_return result::success();
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


kj::Promise<Result<std::map<std::string, std::optional<StorePath>>>> DerivationGoal::queryPartialDerivationOutputMap()
try {
    assert(drv->type().isPure());
    if (!useDerivation || drv->type().hasKnownOutputPaths()) {
        std::map<std::string, std::optional<StorePath>> res;
        for (auto & [name, output] : drv->outputs)
            res.insert_or_assign(name, output.path(worker.store, drv->name, name));
        co_return res;
    } else {
        for (auto * drvStore : {&worker.evalStore, &worker.store}) {
            if (TRY_AWAIT(drvStore->isValidPath(drvPath))) {
                co_return TRY_AWAIT(worker.store.queryPartialDerivationOutputMap(drvPath, drvStore)
                );
            }
        }
        assert(false);
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<OutputPathMap>> DerivationGoal::queryDerivationOutputMap()
try {
    assert(drv->type().isPure());
    if (!useDerivation || drv->type().hasKnownOutputPaths()) {
        OutputPathMap res;
        for (auto & [name, output] : drv->outputsAndOptPaths(worker.store))
            res.insert_or_assign(name, *output.second);
        co_return res;
    } else {
        for (auto * drvStore : {&worker.evalStore, &worker.store}) {
            if (TRY_AWAIT(drvStore->isValidPath(drvPath))) {
                co_return TRY_AWAIT(worker.store.queryDerivationOutputMap(drvPath, drvStore));
            }
        }
        assert(false);
    }
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::pair<bool, SingleDrvOutputs>>> DerivationGoal::checkPathValidity()
try {
    if (!drv->type().isPure()) co_return { false, SingleDrvOutputs{} };

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

    for (auto & i : TRY_AWAIT(queryPartialDerivationOutputMap())) {
        auto initialOutput = get(initialOutputs, i.first);
        if (!initialOutput)
            // this is an invalid output, gets catched with (!wantedOutputsLeft.empty())
            continue;
        auto & info = *initialOutput;
        info.wanted = wantedOutputs.contains(i.first);
        if (info.wanted)
            wantedOutputsLeft.erase(i.first);
        if (i.second) {
            auto outputPath = *i.second;
            info.known = {
                .path = outputPath,
                .status = !TRY_AWAIT(worker.store.isValidPath(outputPath))
                    ? PathStatus::Absent
                    : !checkHash || TRY_AWAIT(worker.pathContentsGood(outputPath))
                    ? PathStatus::Valid
                    : PathStatus::Corrupt,
            };
        }
        auto drvOutput = DrvOutput{info.outputHash, i.first};
        if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
            if (auto real = TRY_AWAIT(worker.store.queryRealisation(drvOutput))) {
                info.known = {
                    .path = real->outPath,
                    .status = PathStatus::Valid,
                };
            } else if (info.known && info.known->isValid()) {
                // We know the output because it's a static output of the
                // derivation, and the output path is valid, but we don't have
                // its realisation stored (probably because it has been built
                // without the `ca-derivations` experimental flag).
                TRY_AWAIT(worker.store.registerDrvOutput(
                    Realisation {
                        drvOutput,
                        info.known->path,
                    }
                ));
            }
        }
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

    auto * nodeP = fullDrv.inputDrvs.findSlot(DerivedPath::Opaque { .path = dg->drvPath });
    if (!nodeP) return;
    auto & outputs = nodeP->value;

    for (auto & outputName : outputs) {
        auto buildResult = dg->buildResult.restrictTo(DerivedPath::Built {
            .drvPath = makeConstantStorePathRef(dg->drvPath),
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
