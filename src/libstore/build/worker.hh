#pragma once
///@file

#include "types.hh"
#include "lock.hh"
#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"

#include <future>
#include <thread>

namespace nix {

/* Forward definition. */
struct DerivationGoal;
struct PathSubstitutionGoal;
class DrvOutputSubstitutionGoal;

typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;

/**
 * A mapping used to remember for each child process to what goal it
 * belongs, and file descriptors for receiving log data and output
 * path creation commands.
 */
struct Child
{
    WeakGoalPtr goal;
    Goal * goal2; // ugly hackery
    std::set<int> fds;
    bool respectTimeouts;
    bool inBuildSlot;
    /**
     * Time we last got output on stdout/stderr
     */
    steady_time_point lastOutput;
    steady_time_point timeStarted;
};

/* Forward definition. */
struct HookInstance;

/**
 * The worker class.
 */
class Worker
{
private:

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
     * Child processes currently running.
     */
    std::list<Child> children;

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

    void goalFinished(GoalPtr goal, Goal::Finished & f);
    void handleWorkResult(GoalPtr goal, Goal::WorkResult how);

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

public:

    const Activity act;
    const Activity actDerivations;
    const Activity actSubstitutions;

    /**
     * Set if at least one derivation had a BuildError (i.e. permanent
     * failure).
     */
    bool permanentFailure;

    /**
     * Set if at least one derivation had a timeout.
     */
    bool timedOut;

    /**
     * Set if at least one derivation fails with a hash mismatch.
     */
    bool hashMismatch;

    /**
     * Set if at least one derivation is not deterministic in check mode.
     */
    bool checkMismatch;

    Store & store;
    Store & evalStore;

    struct HookState {
        std::unique_ptr<HookInstance> instance;

        /**
         * Whether to ask the build hook if it can build a derivation. If
         * it answers with "decline-permanently", we don't try again.
         */
        bool available = true;
    };

    HookState hook;

    uint64_t expectedBuilds = 0;
    uint64_t doneBuilds = 0;
    uint64_t failedBuilds = 0;
    uint64_t runningBuilds = 0;

    uint64_t expectedSubstitutions = 0;
    uint64_t doneSubstitutions = 0;
    uint64_t failedSubstitutions = 0;
    uint64_t runningSubstitutions = 0;
    uint64_t expectedDownloadSize = 0;
    uint64_t doneDownloadSize = 0;
    uint64_t expectedNarSize = 0;
    uint64_t doneNarSize = 0;

    Worker(Store & store, Store & evalStore);
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
public:
    std::shared_ptr<DerivationGoal> makeDerivationGoal(
        const StorePath & drvPath,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal);
    std::shared_ptr<DerivationGoal> makeBasicDerivationGoal(
        const StorePath & drvPath, const BasicDerivation & drv,
        const OutputsSpec & wantedOutputs, BuildMode buildMode = bmNormal);

    /**
     * @ref SubstitutionGoal "substitution goal"
     */
    std::shared_ptr<PathSubstitutionGoal> makePathSubstitutionGoal(const StorePath & storePath, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);
    std::shared_ptr<DrvOutputSubstitutionGoal> makeDrvOutputSubstitutionGoal(const DrvOutput & id, RepairFlag repair = NoRepair, std::optional<ContentAddress> ca = std::nullopt);

    /**
     * Make a goal corresponding to the `DerivedPath`.
     *
     * It will be a `DerivationGoal` for a `DerivedPath::Built` or
     * a `SubstitutionGoal` for a `DerivedPath::Opaque`.
     */
    GoalPtr makeGoal(const DerivedPath & req, BuildMode buildMode = bmNormal);

    /**
     * Remove a dead goal.
     */
    void removeGoal(GoalPtr goal);

    /**
     * Return the number of local build processes currently running (but not
     * remote builds via the build hook).
     */
    unsigned int getNrLocalBuilds();

    /**
     * Return the number of substitution processes currently running.
     */
    unsigned int getNrSubstitutions();

    /**
     * Registers a running child process.  `inBuildSlot` means that
     * the process counts towards the jobs limit.
     */
    void childStarted(GoalPtr goal, const std::set<int> & fds,
        bool inBuildSlot, bool respectTimeouts);

    /**
     * Unregisters a running child process.
     */
    void childTerminated(Goal * goal);

    /**
     * Loop until the specified top-level goals have finished.
     */
    void run(const Goals & topGoals);

    /**
     * Wait for input to become available.
     */
    void waitForInput();

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
