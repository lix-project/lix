#pragma once
///@file

#include "build/local-derivation-goal.hh"
#include "gc-store.hh"
#include "local-store.hh"

namespace nix {

/**
 * FreeBSD-specific implementation of LocalStore
 */
class FreeBSDLocalStore : public LocalStore
{
public:
    FreeBSDLocalStore(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , LocalStoreConfig(params)
        , Store(params)
        , LocalFSStore(params)
        , LocalStore(params)
    {
    }
    FreeBSDLocalStore(const std::string scheme, std::string path, const Params & params)
        : FreeBSDLocalStore(params)
    {
        throw UnimplementedError("FreeBSDLocalStore");
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
