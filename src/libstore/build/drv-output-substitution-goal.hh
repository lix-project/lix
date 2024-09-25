#pragma once
///@file

#include "notifying-counter.hh"
#include "store-api.hh"
#include "goal.hh"
#include "realisation.hh"
#include <future>

namespace nix {

class Worker;

/**
 * Substitution of a derivation output.
 * This is done in three steps:
 * 1. Fetch the output info from a substituter
 * 2. Substitute the corresponding output path
 * 3. Register the output info
 */
class DrvOutputSubstitutionGoal : public Goal {

    /**
     * The drv output we're trying to substitute
     */
    DrvOutput id;

    /**
     * The realisation corresponding to the given output id.
     * Will be filled once we can get it.
     */
    std::shared_ptr<const Realisation> outputInfo;

    /**
     * The remaining substituters.
     */
    std::list<ref<Store>> subs;

    /**
     * The current substituter.
     */
    std::shared_ptr<Store> sub;

    NotifyingCounter<uint64_t>::Bump maintainRunningSubstitutions;

    struct DownloadState
    {
        kj::Own<kj::CrossThreadPromiseFulfiller<void>> outPipe;
        std::future<std::shared_ptr<const Realisation>> result;
    };

    std::shared_ptr<DownloadState> downloadState;

    /**
     * Whether a substituter failed.
     */
    bool substituterFailed = false;

public:
    DrvOutputSubstitutionGoal(
        const DrvOutput & id,
        Worker & worker,
        bool isDependency,
        RepairFlag repair = NoRepair,
        std::optional<ContentAddress> ca = std::nullopt
    );

    typedef kj::Promise<Result<WorkResult>> (DrvOutputSubstitutionGoal::*GoalState)(bool inBuildSlot) noexcept;
    GoalState state;

    kj::Promise<Result<WorkResult>> init(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> tryNext(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> realisationFetched(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> outPathValid(bool inBuildSlot) noexcept;
    kj::Promise<Result<WorkResult>> finished() noexcept;

    kj::Promise<Result<WorkResult>> work() noexcept override;

    JobCategory jobCategory() const override {
        return JobCategory::Substitution;
    };
};

}
