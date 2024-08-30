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
    Pipe outPipe;

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

    typedef WorkResult (PathSubstitutionGoal::*GoalState)(bool inBuildSlot);
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

    Finished timedOut(Error && ex) override { abort(); };

    /**
     * We prepend "a$" to the key name to ensure substitution goals
     * happen before derivation goals.
     */
    std::string key() override
    {
        return "a$" + std::string(storePath.name()) + "$" + worker.store.printStorePath(storePath);
    }

    WorkResult work(bool inBuildSlot) override;

    /**
     * The states.
     */
    WorkResult init(bool inBuildSlot);
    WorkResult tryNext(bool inBuildSlot);
    WorkResult referencesValid(bool inBuildSlot);
    WorkResult tryToRun(bool inBuildSlot);
    WorkResult finished(bool inBuildSlot);

    /**
     * Callback used by the worker to write to the log.
     */
    WorkResult handleChildOutput(int fd, std::string_view data) override;

    /* Called by destructor, can't be overridden */
    void cleanup() override final;

    JobCategory jobCategory() const override {
        return JobCategory::Substitution;
    };
};

}
