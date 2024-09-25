#pragma once
///@file

#include "async-semaphore.hh"
#include "result.hh"
#include "types.hh"
#include "store-api.hh"
#include "build-result.hh"
#include <concepts> // IWYU pragma: keep
#include <kj/async.h>

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

struct CompareGoalPtrs {
    bool operator() (const GoalPtr & a, const GoalPtr & b) const;
};

/**
 * Set of goals.
 */
typedef std::set<GoalPtr, CompareGoalPtrs> Goals;

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

struct Goal
{
    typedef enum {ecSuccess, ecFailed, ecNoSubstituters, ecIncompleteClosure} ExitCode;

    /**
     * Backlink to the worker.
     */
    Worker & worker;

    /**
      * Whether this goal is only a dependency of other goals. Toplevel
      * goals that are also dependencies of other toplevel goals do not
      * set this, only goals that are exclusively dependencies do this.
      */
    const bool isDependency;

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

    // for use by Worker only. will go away once work() is a promise.
    kj::Own<kj::PromiseFulfiller<void>> notify;

protected:
    AsyncSemaphore::Token slotToken;

public:

    struct Finished;

    struct [[nodiscard]] StillAlive {};
    struct [[nodiscard]] ContinueImmediately {};
    struct [[nodiscard]] Finished {
        ExitCode exitCode;
        BuildResult result;
        std::shared_ptr<Error> ex;
        bool permanentFailure = false;
        bool timedOut = false;
        bool hashMismatch = false;
        bool checkMismatch = false;
    };

    struct [[nodiscard]] WorkResult : std::variant<
                                          StillAlive,
                                          ContinueImmediately,
                                          Finished>
    {
        WorkResult() = delete;
        using variant::variant;
    };

protected:
    kj::Promise<Result<WorkResult>> waitForAWhile();
    kj::Promise<Result<WorkResult>>
    waitForGoals(kj::Array<std::pair<GoalPtr, kj::Promise<void>>> dependencies) noexcept;

    template<std::derived_from<Goal>... G>
    kj::Promise<Result<Goal::WorkResult>>
    waitForGoals(std::pair<std::shared_ptr<G>, kj::Promise<void>>... goals) noexcept
    {
        return waitForGoals(kj::arrOf<std::pair<GoalPtr, kj::Promise<void>>>(std::move(goals)...));
    }

public:

    /**
     * Exception containing an error message, if any.
     */
    std::shared_ptr<Error> ex;

    explicit Goal(Worker & worker, bool isDependency)
        : worker(worker)
        , isDependency(isDependency)
    { }

    virtual ~Goal() noexcept(false)
    {
        trace("goal destroyed");
    }

    virtual kj::Promise<Result<WorkResult>> work() noexcept = 0;

    virtual void waiteeDone(GoalPtr waitee) { }

    void trace(std::string_view s);

    std::string getName() const
    {
        return name;
    }

    virtual std::string key() = 0;

    virtual void cleanup() { }

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;
};

}
