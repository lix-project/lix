#pragma once
///@file

#include "types.hh"
#include "store-api.hh"
#include "build-result.hh"

namespace nix {

/**
 * Forward definition.
 */
struct Goal;
class Worker;

/**
 * A pointer to a goal.
 */
typedef std::shared_ptr<Goal> GoalPtr;
typedef std::weak_ptr<Goal> WeakGoalPtr;

struct CompareGoalPtrs {
    bool operator() (const GoalPtr & a, const GoalPtr & b) const;
};

/**
 * Set of goals.
 */
typedef std::set<GoalPtr, CompareGoalPtrs> Goals;
typedef std::set<WeakGoalPtr, std::owner_less<WeakGoalPtr>> WeakGoals;

/**
 * A map of paths to goals (and the other way around).
 */
typedef std::map<StorePath, WeakGoalPtr> WeakGoalMap;

/**
 * Used as a hint to the worker on how to schedule a particular goal. For example,
 * builds are typically CPU- and memory-bound, while substitutions are I/O bound.
 * Using this information, the worker might decide to schedule more or fewer goals
 * of each category in parallel.
 */
enum struct JobCategory {
    /**
     * A build of a derivation; it will use CPU and disk resources.
     */
    Build,
    /**
     * A substitution an arbitrary store object; it will use network resources.
     */
    Substitution,
};

struct Goal : public std::enable_shared_from_this<Goal>
{
    typedef enum {ecSuccess, ecFailed, ecNoSubstituters, ecIncompleteClosure} ExitCode;

    /**
     * Backlink to the worker.
     */
    Worker & worker;

    /**
     * Goals that this goal is waiting for.
     */
    Goals waitees;

    /**
     * Goals waiting for this one to finish.  Must use weak pointers
     * here to prevent cycles.
     */
    WeakGoals waiters;

    /**
     * Number of goals we are/were waiting for that have failed.
     */
    size_t nrFailed = 0;

    /**
     * Number of substitution goals we are/were waiting for that
     * failed because there are no substituters.
     */
    size_t nrNoSubstituters = 0;

    /**
     * Number of substitution goals we are/were waiting for that
     * failed because they had unsubstitutable references.
     */
    size_t nrIncompleteClosure = 0;

    /**
     * Name of this goal for debugging purposes.
     */
    std::string name;

    /**
     * Whether the goal is finished.
     */
    std::optional<ExitCode> exitCode;

    /**
     * Build result.
     */
    BuildResult buildResult;

public:

    struct [[nodiscard]] StillAlive {};
    struct [[nodiscard]] WaitForSlot {};
    struct [[nodiscard]] WaitForAWhile {};
    struct [[nodiscard]] Finished {
        ExitCode result;
        std::unique_ptr<Error> ex;
    };

    struct [[nodiscard]] WorkResult : std::variant<StillAlive, WaitForSlot, WaitForAWhile, Finished>
    {
        WorkResult() = delete;
        using variant::variant;
    };

    /**
     * Exception containing an error message, if any.
     */
    std::unique_ptr<Error> ex;

    Goal(Worker & worker, DerivedPath path)
        : worker(worker)
    { }

    virtual ~Goal() noexcept(false)
    {
        trace("goal destroyed");
    }

    virtual WorkResult work() = 0;

    void addWaitee(GoalPtr waitee);

    virtual void waiteeDone(GoalPtr waitee) { }

    virtual WorkResult handleChildOutput(int fd, std::string_view data)
    {
        abort();
    }

    virtual void handleEOF(int fd)
    {
    }

    void trace(std::string_view s);

    std::string getName() const
    {
        return name;
    }

    /**
     * Callback in case of a timeout.  It should wake up its waiters,
     * get rid of any running child processes that are being monitored
     * by the worker (important!), etc.
     */
    virtual Finished timedOut(Error && ex) = 0;

    virtual std::string key() = 0;

    virtual void cleanup() { }

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;
};

}
