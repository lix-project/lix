#include "lix/libstore/local-store.hh"
#include "lix/libstore/build/local-derivation-goal.hh"

#if __linux__
#include "lix/libstore/platform/linux.hh"
#elif __APPLE__
#include "platform/darwin.hh"
#elif __FreeBSD__
#include "platform/freebsd.hh"
#else
#include "platform/fallback.hh"
#endif

namespace nix {
std::shared_ptr<LocalStore> LocalStore::makeLocalStore(const StoreConfig::Params & params)
{
#if __linux__
    return std::shared_ptr<LocalStore>(new LinuxLocalStore(params));
#elif __APPLE__
    return std::shared_ptr<LocalStore>(new DarwinLocalStore(params));
#elif __FreeBSD__
    return std::shared_ptr<LocalStore>(new FreeBSDLocalStore(params));
#else
    return std::shared_ptr<LocalStore>(new FallbackLocalStore(params));
#endif
}

std::unique_ptr<LocalDerivationGoal> LocalDerivationGoal::makeLocalDerivationGoal(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    Worker & worker,
    bool isDependency,
    BuildMode buildMode
)
{
#if __linux__
    return std::make_unique<LinuxLocalDerivationGoal>(drvPath, wantedOutputs, worker, isDependency, buildMode);
#elif __APPLE__
    return std::make_unique<DarwinLocalDerivationGoal>(drvPath, wantedOutputs, worker, isDependency, buildMode);
#elif __FreeBSD__
    return std::make_unique<FreeBSDLocalDerivationGoal>(drvPath, wantedOutputs, worker, isDependency, buildMode);
#else
    return std::make_unique<FallbackLocalDerivationGoal>(drvPath, wantedOutputs, worker, isDependency, buildMode);
#endif
}

std::unique_ptr<LocalDerivationGoal> LocalDerivationGoal::makeLocalDerivationGoal(
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs,
    Worker & worker,
    bool isDependency,
    BuildMode buildMode
)
{
#if __linux__
    return std::make_unique<LinuxLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, isDependency, buildMode
    );
#elif __APPLE__
    return std::make_unique<DarwinLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, isDependency, buildMode
    );
#elif __FreeBSD__
    return std::make_unique<FreeBSDLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, isDependency, buildMode
    );
#else
    return std::make_unique<FallbackLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, isDependency, buildMode
    );
#endif
}
}