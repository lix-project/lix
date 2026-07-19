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
    FallbackLocalStore(kj::Badge<LocalStore>, LocalStoreConfig config) : Store(config), LocalStore(config) {}
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
