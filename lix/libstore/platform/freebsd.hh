#pragma once
///@file

#include "lix/libstore/build/local-derivation-goal.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/local-store.hh"

namespace nix {

/**
 * FreeBSD-specific implementation of LocalStore
 */
class FreeBSDLocalStore : public LocalStore
{
public:
    FreeBSDLocalStore(kj::Badge<LocalStore>, LocalStoreConfig config, Activity * parent)
        : Store(config, parent)
        , LocalStore(config, parent)
    {
    }

private:

    void findPlatformRoots(UncheckedRoots & unchecked) override;
};

/**
 * FreeBSD-specific implementation of LocalDerivationGoal
 */
class FreeBSDLocalDerivationGoal : public LocalDerivationGoal
{
public:
    using LocalDerivationGoal::LocalDerivationGoal;

private:
};

}
