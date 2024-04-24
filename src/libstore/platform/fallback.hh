#pragma once
///@file

#include "local-store.hh"

namespace nix {

/**
 * Fallback platform implementation of LocalStore
 * Exists so we can make LocalStore constructor protected
 */
class FallbackLocalStore : public LocalStore
{
public:
    FallbackLocalStore(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , LocalStore(params)
    {
    }
    FallbackLocalStore(const std::string scheme, std::string path, const Params & params)
        : FallbackLocalStore(params)
    {
        throw UnimplementedError("FallbackLocalStore");
    }
};

}
