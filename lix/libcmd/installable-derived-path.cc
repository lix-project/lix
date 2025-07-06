#include "lix/libcmd/installable-derived-path.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

std::string InstallableDerivedPath::what() const
{
    return derivedPath.to_string(*store);
}

DerivedPathsWithInfo InstallableDerivedPath::toDerivedPaths(EvalState & state)
{
    return {{
        .path = derivedPath,
        .info = make_ref<ExtraPathInfo>(),
    }};
}

std::optional<StorePath> InstallableDerivedPath::getStorePath()
{
    return derivedPath.getBaseStorePath();
}

InstallableDerivedPath InstallableDerivedPath::parse(
    ref<Store> store,
    std::string_view prefix,
    ExtendedOutputsSpec extendedOutputsSpec)
{
    auto derivedPath = std::visit(
        overloaded{
            // If the user did not use ^, we treat the output more
            // liberally: we accept a symlink chain or an actual
            // store path.
            [&](const ExtendedOutputsSpec::Default &) -> DerivedPath {
                return DerivedPath::Opaque{
                    .path = store->followLinksToStorePath(prefix),
                };
            },
            // If the user did use ^, we just do exactly what is written.
            [&](const ExtendedOutputsSpec::Explicit & outputSpec) -> DerivedPath {
                auto drv = DerivedPathOpaque::parse(*store, prefix);
                return DerivedPath::Built{
                    .drvPath = std::move(drv),
                    .outputs = outputSpec,
                };
            },
        },
        extendedOutputsSpec.raw
    );
    return InstallableDerivedPath {
        store,
        std::move(derivedPath),
    };
}

}
