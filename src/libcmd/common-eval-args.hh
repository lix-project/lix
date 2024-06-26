#pragma once
///@file

#include "args.hh"
#include "common-args.hh"
#include "search-path.hh"

namespace nix {

class Store;
class EvalState;
class Bindings;
struct SourcePath;

struct MixEvalArgs : virtual Args, virtual MixRepair
{
    static constexpr auto category = "Common evaluation options";

    MixEvalArgs();

    Bindings * getAutoArgs(EvalState & state);

    SearchPath searchPath;

    std::optional<std::string> evalStoreUrl;

private:
    std::map<std::string, std::string> autoArgs;
};

/** @brief Resolve an argument that is generally a file, but could be something that
 * is easy to resolve to a file, like a <lookup path> or a tarball URL.
 *
 * In particular, this will resolve and fetch pseudo-URLs starting with
 * @c channel:, flakerefs starting with @c flake:, and anything that
 * @ref nix::fetchers::downloadTarball() can take.
 *
 * Non-absolute files are looked up relative to the current directory(?)
 * FIXME: the process's current directory or EvalState's current directory?
 *
 * @param state The nix::EvalState to base settings, store, and nixPath from.
 *
 * @param fileArg The the path-ish to resolve.
 *
 * @return A nix::SourcePath to the resolved and fetched file.
 *
 * @exception nix::FileTransferError from nix::fetchers::downloadTarball(). Probably others.
 *
 * @exception nix::ThrownError for failed search path lookup. Probably others.
 */
SourcePath lookupFileArg(EvalState & state, std::string_view fileArg);

}
