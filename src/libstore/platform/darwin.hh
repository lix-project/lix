#pragma once
///@file

#include "gc-store.hh"
#include "local-store.hh"

namespace nix {

/**
 * Darwin-specific implementation of LocalStore
 */
class DarwinLocalStore : public LocalStore
{
public:
    DarwinLocalStore(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , LocalStore(params)
    {
    }
    DarwinLocalStore(const std::string scheme, std::string path, const Params & params)
        : DarwinLocalStore(params)
    {
        throw UnimplementedError("DarwinLocalStore");
    }

private:

    void findPlatformRoots(UncheckedRoots & unchecked) override;
};

}
