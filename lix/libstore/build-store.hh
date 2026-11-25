#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {
class BuildStore : public virtual Store
{
public:
    static constexpr std::string_view operationName = "building, substituting and repairing";

    BuildStore(const StoreConfig & config, Activity * parent) : Store(config, parent) {}

    /**
     * If requested, substitute missing paths. This
     * implements nix-copy-closure's --use-substitutes
     * flag.
     */
    kj::Promise<Result<void>> substitutePaths(const StorePathSet & paths);

    /**
     * For each path, if it's a derivation, build it.  Building a
     * derivation means ensuring that the output paths are valid.  If
     * they are already valid, this is a no-op.  Otherwise, validity
     * can be reached in two ways.  First, if the output paths is
     * substitutable, then build the path that way.  Second, the
     * output paths can be created by running the builder, after
     * recursively building any sub-derivations. For inputs that are
     * not derivations, substitute them.
     */
    virtual kj::Promise<Result<void>> buildPaths(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr
    );

    /**
     * Like buildPaths(), but return a vector of \ref BuildResult
     * BuildResults corresponding to each element in paths. Note that in
     * case of a build/substitution error, this function won't throw an
     * exception, but return a BuildResult containing an error message.
     */
    virtual kj::Promise<Result<std::vector<KeyedBuildResult>>> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr
    );

    /**
     * Build a single non-materialized derivation (i.e. not from an
     * on-disk .drv file).
     *
     * @param drvPath This is used to deduplicate worker goals so it is
     * imperative that is correct. That said, it doesn't literally need
     * to be store path that would be calculated from writing this
     * derivation to the store: it is OK if it instead is that of a
     * Derivation which would resolve to this (by taking the outputs of
     * it's input derivations and adding them as input sources) such
     * that the build time referenceable-paths are the same.
     *
     * In the input-addressed case, we usually *do* use an "original"
     * unresolved derivations's path, as that is what will be used in the
     * buildPaths case. Also, the input-addressed output paths are verified
     * only by that contents of that specific unresolved derivation, so it is
     * nice to keep that information around so if the original derivation is
     * ever obtained later, it can be verified whether the trusted user in fact
     * used the proper output path.
     *
     * In the content-addressed case, we want to always use the resolved
     * drv path calculated from the provided derivation. This serves two
     * purposes:
     *
     *   - It keeps the operation trustless, by ruling out a maliciously
     *     invalid drv path corresponding to a non-resolution-equivalent
     *     derivation.
     *
     *   - For the floating case in particular, it ensures that the derivation
     *     to output mapping respects the resolution equivalence relation, so
     *     one cannot choose different resolution-equivalent derivations to
     *     subvert dependency coherence (i.e. the property that one doesn't end
     *     up with multiple different versions of dependencies without
     *     explicitly choosing to allow it).
     */
    virtual kj::Promise<Result<BuildResult>>
    buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode = bmNormal);

    /**
     * Ensure that a path is valid.  If it is not currently valid, it
     * may be made valid by running a substitute (if defined for the
     * path).
     */
    kj::Promise<Result<void>> ensurePath(const StorePath & path);

    /**
     * Repair the contents of the given path by redownloading it using
     * a substituter (if available).
     */
    virtual kj::Promise<Result<void>> repairPath(const StorePath & path);

    /**
     * Read a derivation, after ensuring its existence through
     * ensurePath().
     */
    kj::Promise<Result<Derivation>> derivationFromPath(const StorePath & drvPath);

    /**
     * Computes the full closure of of a set of store-paths for e.g.
     * derivations that need this information for `exportReferencesGraph`.
     */
    kj::Promise<Result<StorePathSet>>
    exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths);
};
}
