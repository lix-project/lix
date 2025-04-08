#pragma once
///@file

#include "lix/libcmd/built-path.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/flake/flakeref.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/json-fwd.hh"

#include <string>
#include <set>

namespace nix
{

struct ProfileElementSource
{
    FlakeRef originalRef;
    // FIXME: record original attrpath.
    FlakeRef lockedRef;
    std::string attrPath;
    ExtendedOutputsSpec outputs;

    bool operator<(const ProfileElementSource & other) const;

    std::string to_string() const;
};

constexpr int DEFAULT_PRIORITY = 5;

struct ProfileElement
{
    StorePathSet storePaths;
    std::optional<ProfileElementSource> source;
    bool active = true;
    NixInt::Inner priority = DEFAULT_PRIORITY;

    std::string identifier() const;

    /**
     * Return a string representing an installable corresponding to the current
     * element, either a flakeref or a plain store path
     */
    std::set<std::string> toInstallables(Store & store);

    std::string versions() const;

    bool operator<(const ProfileElement & other) const;

    void updateStorePaths(ref<Store> evalStore, ref<Store> store, const BuiltPaths & builtPaths);
};

struct ProfileManifest
{
    std::map<std::string, ProfileElement> elements;

    ProfileManifest() { }

    ProfileManifest(EvalState & state, const Path & profile);

    JSON toJSON(Store & store) const;

    kj::Promise<Result<StorePath>> build(ref<Store> store);

    void addElement(std::string_view nameCandidate, ProfileElement element);
    void addElement(ProfileElement element);

    static void printDiff(const ProfileManifest & prev, const ProfileManifest & cur, std::string_view indent);
};

DrvInfos queryInstalled(EvalState & state, const Path & userEnv);
std::string showVersions(const std::set<std::string> & versions);

}
