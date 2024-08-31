#pragma once
///@file

#include "notifying-counter.hh"
#include "types.hh"
#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"

#include <future>
#include <kj/async-io.h>
#include <thread>

namespace nix {

/* Forward definition. */
struct DerivationGoal;
struct PathSubstitutionGoal;
class DrvOutputSubstitutionGoal;

typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;

/* Forward definition. */
struct HookInstance;

class GoalFactory
{
public:
    virtual std::shared_ptr<DerivationGoal> makeDerivationGoal(
        const StorePath & drvPath, const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal
    ) = 0;
    virtual std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(
        const StorePath & drvPath,
        const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs,
        BuildMode buildMode = bmNormal
    ) = 0;

    /**
     * @ref SubstitutionGoal "substitution goal"
     */
    virtual std::shared_ptr<PathSubstitutionGoal> makePathSubstitutionGoal(
        const StorePath & storePath,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    ) = 0;
    virtual std::shared_ptr<DrvOutputSubstitutionGoal> makeDrvOutputSubstitutionGoal(
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
    virtual GoalPtr makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal) = 0;
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
private:

    bool running = false;

    /* Note: the worker should only have strong pointers to the
       top-level goals. */

    /**
     * The top-level goals of the worker.
     */
    Goals topGoals;

    /**
     * Goals that are ready to do some work.
     */
    WeakGoals awake;

    /**
     * Goals waiting for a build slot.
     */
    WeakGoals wantingToBuild;

    /**
     * Number of build slots occupied.  This includes local builds but does not
     * include substitutions or remote builds via the build hook.
     */
    unsigned int nrLocalBuilds;

    /**
     * Number of substitution slots occupied.
     */
    unsigned int nrSubstitutions;

    /**
     * Maps used to prevent multiple instantiations of a goal for the
     * same derivation / path.
     */
    std::map<StorePath, std::weak_ptr<DerivationGoal>> derivationGoals;
    std::map<StorePath, std::weak_ptr<PathSubstitutionGoal>> substitutionGoals;
    std::map<DrvOutput, std::weak_ptr<DrvOutputSubstitutionGoal>> drvOutputSubstitutionGoals;

    /**
     * Goals sleeping for a few seconds (polling a lock).
     */
    WeakGoals waitingForAWhile;

    /**
     * Last time the goals in `waitingForAWhile` where woken up.
     */
    steady_time_point lastWokenUp;

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

    void goalFinished(GoalPtr goal, Goal::Finished & f);
    void handleWorkResult(GoalPtr goal, Goal::WorkResult how);

    kj::Own<kj::PromiseFulfiller<void>> childFinished;

    /**
     * Put `goal` to sleep until a build slot becomes available (which
     * might be right away).
     */
    void waitForBuildSlot(GoalPtr goal);

    /**
     * Wait for a few seconds and then retry this goal.  Used when
     * waiting for a lock held by another process.  This kind of
     * polling is inefficient, but POSIX doesn't really provide a way
     * to wait for multiple locks in the main select() loop.
     */
    void waitForAWhile(GoalPtr goal);

    /**
     * Wake up a goal (i.e., there is something for it to do).
     */
    void wakeUp(GoalPtr goal);

    /**
     * Wait for input to become available.
     */
    void waitForInput();

    /**
     * Remove a dead goal.
     */
    void removeGoal(GoalPtr goal);

    /**
     * Registers a running child process.  `inBuildSlot` means that
     * the process counts towards the jobs limit.
     */
    void childStarted(GoalPtr goal, kj::Promise<Outcome<void, Goal::Finished>> promise,
        bool inBuildSlot);

    /**
     * Unregisters a running child process.
     */
    void childTerminated(GoalPtr goal, bool inBuildSlot);

    /**
      * Pass current stats counters to the logger for progress bar updates.
      */
    void updateStatistics();

    bool statisticsOutdated = true;

    /**
      * Mark statistics as outdated, such that `updateStatistics` will be called.
      */
    void updateStatisticsLater()
    {
        statisticsOutdated = true;
    }

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    Store & store;
    Store & evalStore;
    kj::AsyncIoContext & aio;

private:
    kj::TaskSet children;
    std::exception_ptr childException;

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

    Worker(Store & store, Store & evalStore, kj::AsyncIoContext & aio);
    ~Worker();

    /**
     * Make a goal (with caching).
     */

    /**
     * @ref DerivationGoal "derivation goal"
     */
private:
    std::shared_ptr<DerivationGoal> makeDerivationGoalCommon(
        const StorePath & drvPath, const OutputsSpec & wantedOutputs,
        std::function<std::shared_ptr<DerivationGoal>()> mkDrvGoal);
    std::shared_ptr<DerivationGoal> makeDerivationGoal(
        const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal) override;
    std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(
        const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal) override;

    /**
     * @ref SubstitutionGoal "substitution goal"
     */
    std::shared_ptr<PathSubstitutionGoal> makePathSubstitutionGoal(const StorePath & storePath, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt) override;
    std::shared_ptr<DrvOutputSubstitutionGoal> makeDrvOutputSubstitutionGoal(const DrvOutput & id, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt) override;

    /**
     * Make a goal corresponding to the `DerivedPath`.
     *
     * It will be a `DerivationGoal` for a `DerivedPath::Built` or
     * a `SubstitutionGoal` for a `DerivedPath::Opaque`.
     */
    GoalPtr makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal) override;

public:
    /**
     * Loop until the specified top-level goals have finished.
     */
    Goals run(std::function<Goals (GoalFactory &)> req);

    /***
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
    unsigned int failingExitStatus();

    /**
     * Check whether the given valid path exists and has the right
     * contents.
     */
    bool pathContentsGood(const StorePath & path);

    void markContentsGood(const StorePath & path);
};

}
