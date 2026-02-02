#pragma once
///@file

#include "lix/libstore/build/local-derivation-goal.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/local-store.hh"

namespace nix {

/**
 * Darwin-specific implementation of LocalStore
 */
class DarwinLocalStore : public LocalStore
{
public:
    DarwinLocalStore(LocalStoreConfig config)
        : Store(config), LocalStore(config)
    {
    }
    DarwinLocalStore(const std::string scheme, std::string path, LocalStoreConfig config)
        : DarwinLocalStore(config)
    {
        throw UnimplementedError("DarwinLocalStore");
    }

private:

    kj::Promise<Result<void>> findPlatformRoots(UncheckedRoots & unchecked) override;
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
     * no-op. sandbox profiles are generated in fillBuilderConfig, we only need
     * to override this to signal that sandboxed builds are actually supported.
     */
    void prepareSandbox() override {}

    void fillBuilderConfig(build::Request::Builder config) override;

    void finishChildSetup(build::Request::Reader request) override;

    /**
     * Set process flags to enter or leave rosetta, then execute the builder
     */
    void execBuilder(build::Request::Reader request) override;

    /**
     * Whether we need to rewrite output hashes.
     * Always true on Darwin since Darwin requires hash rewriting
     * even when sandboxing is enabled.
     */
    bool needsHashRewrite() override { return true; };
};

}
