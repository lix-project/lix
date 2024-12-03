#pragma once
///@file

#include "lix/libcmd/installables.hh"

namespace nix {

struct InstallableDerivedPath : Installable
{
    ref<Store> store;
    DerivedPath derivedPath;

    InstallableDerivedPath(ref<Store> store, DerivedPath && derivedPath)
        : store(store), derivedPath(std::move(derivedPath))
    { }

    std::string what() const override;

    DerivedPathsWithInfo toDerivedPaths(EvalState & state) override;

    std::optional<StorePath> getStorePath() override;

    static InstallableDerivedPath parse(
        ref<Store> store,
        std::string_view prefix,
        ExtendedOutputsSpec extendedOutputsSpec);
};

}
