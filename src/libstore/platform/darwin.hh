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
     * Prepare the sandbox: This is empty on Darwin since sandbox setup happens in
     * enterSandbox
     */
    void prepareSandbox() override{};

    /**
     * Set process flags to enter or leave rosetta, then execute the builder
     */
    void execBuilder(std::string builder, Strings args, Strings envStrs) override;

    /**
     * Whether we need to rewrite output hashes.
     * Always true on Darwin since Darwin requires hash rewriting
     * even when sandboxing is enabled.
     */
    bool needsHashRewrite() override { return true; };
};

}
