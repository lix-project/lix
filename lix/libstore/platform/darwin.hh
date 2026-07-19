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
    DarwinLocalStore(kj::Badge<LocalStore>, LocalStoreConfig config) : Store(config), LocalStore(config) {}

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

    /**
     * Whether we need to rewrite output hashes.
     * Always true on Darwin since Darwin requires hash rewriting
     * even when sandboxing is enabled.
     */
    bool needsHashRewrite() override { return true; };
};

}
