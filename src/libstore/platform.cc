#include "local-store.hh"

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
}
