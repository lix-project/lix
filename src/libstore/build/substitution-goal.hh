#pragma once
///@file

#include "lock.hh"
#include "notifying-counter.hh"
#include "store-api.hh"
#include "goal.hh"

namespace nix {

class Worker;

struct PathSubstitutionGoal : public Goal
{
    /**
     * The store path that should be realised through a substitute.
     */
    StorePath storePath;

    /**
     * The path the substituter refers to the path as. This will be
     * different when the stores have different names.
     */
    std::optional<StorePath> subPath;

    /**
     * The remaining substituters.
     */
    std::list<ref<Store>> subs;

    /**
     * The current substituter.
     */
    std::shared_ptr<Store> sub;

    /**
     * Whether a substituter failed.
     */
    bool substituterFailed = false;

    /**
     * Path info returned by the substituter's query info operation.
     */
    std::shared_ptr<const ValidPathInfo> info;

    /**
     * Pipe for the substituter's standard output.
     */
    kj::Own<kj::CrossThreadPromiseFulfiller<void>> outPipe;

    /**
     * The substituter thread.
     */
    std::future<void> thr;

    /**
     * Whether to try to repair a valid path.
     */
    RepairFlag repair;

    /**
     * Location where we're downloading the substitute.  Differs from
     * storePath when doing a repair.
     */
    Path destPath;

    NotifyingCounter<uint64_t>::Bump maintainExpectedSubstitutions,
        maintainRunningSubstitutions, maintainExpectedNar, maintainExpectedDownload;

    typedef kj::Promise<Result<WorkResult>> (PathSubstitutionGoal::*GoalState)(bool inBuildSlot) noexcept;
    GoalState state;

    /**
     * Content address for recomputing store path
     */
    std::optional<ContentAddress> ca;

    Finished done(
        ExitCode result,
        BuildResult::Status status,
        std::optional<std::string> errorMsg = {});

public:
    PathSubstitutionGoal(
        const StorePath & storePath,
        Worker & worker,
        bool isDependency,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    );
    ~PathSubstitutionGoal();

    /**
     * We prepend "a$" to the key name to ensure substitution goals
     * happen before derivation goals.
     */
    std::string key() override
    {
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    kj::Promise<Result<WorkResult>> work(bool inBuildSlot) noexcept override;

    /**
     * The states.
     */
    kj::Promise<Result<WorkResult>> init(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> tryNext(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> referencesValid(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> tryToRun(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> finished(bool inBuildSlot) noexcept;

    /* Called by destructor, can't be overridden */
    void cleanup() override final;

    JobCategory jobCategory() const override {
        return JobCategory::Substitution;
    };
};

}
