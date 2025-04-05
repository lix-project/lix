#pragma once
///@file

#include "lix/libutil/async-semaphore.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/build-result.hh"
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

/**
 * Set of goals.
 */
typedef std::set<GoalPtr> Goals;

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

protected:
    AsyncSemaphore::Token slotToken;

public:
    struct [[nodiscard]] WorkResult {
        ExitCode exitCode;
        BuildResult result = {};
        std::shared_ptr<Error> ex = {};
        bool permanentFailure = false;
        bool timedOut = false;
        bool hashMismatch = false;
        bool checkMismatch = false;
        /// Store path this goal relates to. Will be set to drvPath for
        /// derivations, or the substituted store path for substitions.
        std::optional<StorePath> storePath = {};
    };

protected:
    kj::Promise<void> waitForAWhile();
    kj::Promise<Result<void>>
    waitForGoals(kj::Array<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies) noexcept;

    template<std::derived_from<Goal>... G>
    kj::Promise<Result<void>>
    waitForGoals(std::pair<std::shared_ptr<G>, kj::Promise<Result<WorkResult>>>... goals) noexcept
    {
        return waitForGoals(
            kj::arrOf<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>>(std::move(goals)...)
        );
    }

    virtual kj::Promise<Result<WorkResult>> workImpl() noexcept = 0;

    std::string lixAsyncTaskContext() const
    {
        return name;
    }

public:
    explicit Goal(Worker & worker, bool isDependency)
        : worker(worker)
        , isDependency(isDependency)
    { }

    virtual ~Goal() noexcept(false)
    {
        trace("goal destroyed");
    }

    kj::Promise<Result<WorkResult>> work() noexcept;

    virtual void waiteeDone(GoalPtr waitee) { }

    void trace(std::string_view s);

    std::string getName() const
    {
        return name;
    }

    virtual void cleanup() { }

    /**
     * @brief Hint for the scheduler, which concurrency limit applies.
     * @see JobCategory
     */
    virtual JobCategory jobCategory() const = 0;
};

}
