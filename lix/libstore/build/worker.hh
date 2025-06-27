#pragma once
///@file

#include "lix/libutil/async-semaphore.hh"
#include "lix/libutil/concepts.hh"
#include "lix/libutil/notifying-counter.hh"
#include "lix/libutil/types.hh"
#include "lix/libstore/lock.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build/goal.hh"
#include "lix/libstore/realisation.hh"

#include <future>
#include <kj/async-io.h>
#include <thread>

namespace nix {

/* Forward definition. */
struct DerivationGoal;
struct PathSubstitutionGoal;
class DrvOutputSubstitutionGoal;
class LocalStore;

typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;

/* Forward definition. */
struct HookInstance;

class GoalFactory
{
public:
    virtual std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makeDerivationGoal(
        const StorePath & drvPath, const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal
    ) = 0;
    virtual std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makeBasicDerivationGoal(
        const StorePath & drvPath,
        const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs,
        BuildMode buildMode = bmNormal
    ) = 0;

    /**
     * @ref SubstitutionGoal "substitution goal"
     */
    virtual std::pair<std::shared_ptr<PathSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makePathSubstitutionGoal(
        const StorePath & storePath,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    ) = 0;
    virtual std::pair<std::shared_ptr<DrvOutputSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makeDrvOutputSubstitutionGoal(
        const DrvOutput & id,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    ) = 0;

    /**
     * Make a goal corresponding to the `DerivedPath`.
     *
     * It will be a `DerivationGoal` for a `DerivedPath::Built` or
     * a `SubstitutionGoal` for a `DerivedPath::Opaque`.
     */
    virtual std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>>
    makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal) = 0;
};

// elaborate hoax to let goals access factory methods while hiding them from the public
class WorkerBase : protected GoalFactory
{
    friend struct DerivationGoal;
    friend struct PathSubstitutionGoal;
    friend class DrvOutputSubstitutionGoal;

protected:
    GoalFactory & goalFactory() { return *this; }
};

/**
 * The worker class.
 */
class Worker : public WorkerBase
{
public:
    using Targets = std::vector<std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>>>;
    struct Results {
        /** Results of individual goals, if available. Goal results will be
          * added to this map with the index they had in the `Targets` list
          * returned by the goal factory function passed to `work`. If some
          * goals did not complete processing, e.g. due to an early exit on
          * goal failures, not all indices will be set. This may be used to
          * detect which of the goals were cancelled before they completed.
          */
        std::map<size_t, Goal::WorkResult> goals;

        /**
         * The exit status in case of failure.
         *
         * In the case of a build failure, returned value follows this
         * bitmask:
         *
         * ```
         * 0b1100100
         *      ^^^^
         *      |||`- timeout
         *      ||`-- output hash mismatch
         *      |`--- build failure
         *      `---- not deterministic
         * ```
         *
         * In other words, the failure code is at least 100 (0b1100100), but
         * might also be greater.
         *
         * Otherwise (no build failure, but some other sort of failure by
         * assumption), this returned value is 1.
         */
        unsigned int failingExitStatus;
    };

private:

    bool running = false;

    template<typename G>
    struct CachedGoal
    {
        std::shared_ptr<G> goal;
        kj::ForkedPromise<Result<Goal::WorkResult>> promise{nullptr};
    };
    /**
     * Maps used to prevent multiple instantiations of a goal for the
     * same derivation / path.
     */
    std::map<StorePath, CachedGoal<DerivationGoal>> derivationGoals;
    std::map<StorePath, CachedGoal<PathSubstitutionGoal>> substitutionGoals;
    std::map<DrvOutput, CachedGoal<DrvOutputSubstitutionGoal>> drvOutputSubstitutionGoals;

    /**
     * Cache for pathContentsGood().
     */
    std::map<StorePath, bool> pathContentsGoodCache;

    /**
     * Set if at least one derivation had a BuildError (i.e. permanent
     * failure).
     */
    bool permanentFailure = false;

    /**
     * Set if at least one derivation had a timeout.
     */
    bool timedOut = false;

    /**
     * Set if at least one derivation fails with a hash mismatch.
     */
    bool hashMismatch = false;

    /**
     * Set if at least one derivation is not deterministic in check mode.
     */
    bool checkMismatch = false;

    /**
      * Pass current stats counters to the logger for progress bar updates.
      */
    kj::Promise<Result<Results>> updateStatistics();

    AsyncSemaphore statisticsUpdateSignal{1};
    std::optional<AsyncSemaphore::Token> statisticsUpdateInhibitor;

    /**
      * Mark statistics as outdated, such that `updateStatistics` will be called.
      */
    void updateStatisticsLater()
    {
        statisticsUpdateInhibitor = {};
    }

    kj::Promise<Result<Results>> runImpl(Targets topGoals);
    kj::Promise<Result<Results>> boopGC(LocalStore & localStore);

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    Store & store;
    Store & evalStore;
    kj::AsyncIoContext & aio;
    AsyncSemaphore substitutions, localBuilds;
    std::optional<Path> buildDirOverride;

private:
    kj::TaskSet children;

public:
    struct HookState {
        std::unique_ptr<HookInstance> instance;

        /**
         * Whether to ask the build hook if it can build a derivation. If
         * it answers with "decline-permanently", we don't try again.
         */
        bool available = true;
    };

    HookState hook;

    NotifyingCounter<uint64_t> expectedBuilds{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> doneBuilds{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> failedBuilds{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> runningBuilds{[this] { updateStatisticsLater(); }};

    NotifyingCounter<uint64_t> expectedSubstitutions{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> doneSubstitutions{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> failedSubstitutions{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> runningSubstitutions{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> expectedDownloadSize{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> doneDownloadSize{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> expectedNarSize{[this] { updateStatisticsLater(); }};
    NotifyingCounter<uint64_t> doneNarSize{[this] { updateStatisticsLater(); }};

private:
    Worker(Store & store, Store & evalStore, kj::AsyncIoContext & aio);
    ~Worker();

    /**
     * Make a goal (with caching).
     */

    /**
     * @ref DerivationGoal "derivation goal"
     */
    template<typename ID, std::derived_from<Goal> G>
    std::pair<std::shared_ptr<G>, kj::Promise<Result<Goal::WorkResult>>> makeGoalCommon(
        std::map<ID, CachedGoal<G>> & map,
        const ID & key,
        InvocableR<std::unique_ptr<G>> auto create,
        InvocableR<bool, G &> auto modify
    );
    std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>> makeDerivationGoal(
        const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal) override;
    std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>> makeBasicDerivationGoal(
        const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal) override;

    /**
     * @ref SubstitutionGoal "substitution goal"
     */
    std::pair<std::shared_ptr<PathSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makePathSubstitutionGoal(
        const StorePath & storePath,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    ) override;
    std::pair<std::shared_ptr<DrvOutputSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
    makeDrvOutputSubstitutionGoal(
        const DrvOutput & id,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    ) override;

    /**
     * Make a goal corresponding to the `DerivedPath`.
     *
     * It will be a `DerivationGoal` for a `DerivedPath::Built` or
     * a `SubstitutionGoal` for a `DerivedPath::Opaque`.
     */
    std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>>
    makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal) override;

public:
    /**
     * Loop until the specified top-level goals have finished.
     */
    kj::Promise<Result<Results>> run(std::function<Targets (GoalFactory &)> req);

    /**
     * Check whether the given valid path exists and has the right
     * contents.
     */
    bool pathContentsGood(const StorePath & path);

    void markContentsGood(const StorePath & path);

    template<typename MkGoals>
    friend kj::Promise<Result<Results>> processGoals(
        Store & store, Store & evalStore, kj::AsyncIoContext & aio, MkGoals && mkGoals
    ) noexcept;
};

template<typename MkGoals>
kj::Promise<Result<Worker::Results>> processGoals(
    Store & store, Store & evalStore, kj::AsyncIoContext & aio, MkGoals && mkGoals
) noexcept
try {
    co_return co_await Worker(store, evalStore, aio).run(std::forward<MkGoals>(mkGoals));
} catch (...) {
    co_return result::failure(std::current_exception());
}
}
