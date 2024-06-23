#include "local-store.hh"
#include "build/local-derivation-goal.hh"

#if __linux__
#include "platform/linux.hh"
#elif __APPLE__
#include "platform/darwin.hh"
#else
#include "platform/fallback.hh"
#endif

namespace nix {
std::shared_ptr<LocalStore> LocalStore::makeLocalStore(const Params & params)
{
#if __linux__
    return std::shared_ptr<LocalStore>(new LinuxLocalStore(params));
#elif __APPLE__
    return std::shared_ptr<LocalStore>(new DarwinLocalStore(params));
#else
    return std::shared_ptr<LocalStore>(new FallbackLocalStore(params));
#endif
}

std::shared_ptr<LocalDerivationGoal> LocalDerivationGoal::makeLocalDerivationGoal(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    Worker & worker,
    BuildMode buildMode
)
{
#if __linux__
    return std::make_shared<LinuxLocalDerivationGoal>(drvPath, wantedOutputs, worker, buildMode);
#elif __APPLE__
    return std::make_shared<DarwinLocalDerivationGoal>(drvPath, wantedOutputs, worker, buildMode);
#else
    return std::make_shared<FallbackLocalDerivationGoal>(drvPath, wantedOutputs, worker, buildMode);
#endif
}

std::shared_ptr<LocalDerivationGoal> LocalDerivationGoal::makeLocalDerivationGoal(
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs,
    Worker & worker,
    BuildMode buildMode
)
{
#if __linux__
    return std::make_shared<LinuxLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, buildMode
    );
#elif __APPLE__
    return std::make_shared<DarwinLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, buildMode
    );
#else
    return std::make_shared<FallbackLocalDerivationGoal>(
        drvPath, drv, wantedOutputs, worker, buildMode
    );
#endif
}
}
