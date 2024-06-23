#pragma once
///@file

#include "build/local-derivation-goal.hh"
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

/**
 * Darwin-specific implementation of LocalDerivationGoal
 */
class DarwinLocalDerivationGoal : public LocalDerivationGoal
{
public:
    using LocalDerivationGoal::LocalDerivationGoal;

private:
    /**
     * Set process flags to enter or leave rosetta, then execute the builder
     */
    void execBuilder(std::string builder, Strings args, Strings envStrs) override;
};

}
