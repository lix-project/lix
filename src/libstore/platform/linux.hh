#pragma once
///@file

#include "gc-store.hh"
#include "local-store.hh"

namespace nix {

/**
 * Linux-specific implementation of LocalStore
 */
class LinuxLocalStore : public LocalStore
{
public:
    LinuxLocalStore(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , LocalStore(params)
    {
    }
    LinuxLocalStore(const std::string scheme, std::string path, const Params & params)
        : LinuxLocalStore(params)
    {
        throw UnimplementedError("LinuxLocalStore");
    }

private:

    void findPlatformRoots(UncheckedRoots & unchecked) override;
};

}
