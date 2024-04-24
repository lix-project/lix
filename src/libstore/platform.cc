#include "local-store.hh"

#if __linux__
#include "platform/linux.hh"
#else
#include "platform/fallback.hh"
#endif

namespace nix {
std::shared_ptr<LocalStore> LocalStore::makeLocalStore(const Params & params)
{
#if __linux__
    return std::shared_ptr<LocalStore>(new LinuxLocalStore(params));
#else
    return std::shared_ptr<LocalStore>(new FallbackLocalStore(params));
#endif
}
}
