#pragma once
///@file

#include "lix/libstore/build/local-derivation-goal.hh"
#include "lix/libstore/local-store.hh"

namespace nix {

/**
 * Fallback platform implementation of LocalStore
 * Exists so we can make LocalStore constructor protected
 */
class FallbackLocalStore : public LocalStore
{
public:
    FallbackLocalStore(LocalStoreConfig config) : Store(config), LocalStore(config) {}
    FallbackLocalStore(const std::string scheme, std::string path, LocalStoreConfig config)
        : FallbackLocalStore(config)
    {
        throw UnimplementedError("FallbackLocalStore");
    }
};

/**
 * Fallback platform implementation of LocalDerivationGoal
 * Exists so we can make LocalDerivationGoal constructor protected
 */
class FallbackLocalDerivationGoal : public LocalDerivationGoal
{
public:
    using LocalDerivationGoal::LocalDerivationGoal;
};

}
