#include "lix/libexpr/eval-settings.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/registry.hh"
#include "lix/libexpr/flake/flakeref.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libcmd/command.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/regex.hh"

#include <regex>

namespace nix {

static std::regex const identifierRegex = regex::parse("^[A-Za-z_][A-Za-z0-9_'-]*$");
static void checkValidNixIdentifier(const std::string & name)
{
    std::smatch match;
    if (!std::regex_match(name, match, identifierRegex)) {
        throw UsageError(
            "This invocation specifies a value for argument '%s' "
            "which isn't a valid Nix identifier. "
            "The project is dropping support for this so that it's possible to make e.g. "
            "'%s' evaluating to '%s' in the future. "
            "If you depend on this behavior, please reach out in "
            "<https://git.lix.systems/lix-project/lix/issues/496> so we can discuss your use-case.",
            name,
            "--arg config.allowUnfree true",
            "{ config.allowUnfree = true; }"
        );
    }
}

MixEvalArgs::MixEvalArgs()
{
    addFlag(
        {.longName = "arg",
         .description = "Pass the value *expr* as the argument *name* to Nix functions.",
         .category = category,
         .labels = {"name", "expr"},
         .handler = {[&](std::string name, std::string expr) {
             checkValidNixIdentifier(name);
             autoArgs[name] = 'E' + expr;
         }}}
    );

    addFlag({
        .longName = "argstr",
        .description = "Pass the string *string* as the argument *name* to Nix functions.",
        .category = category,
        .labels = {"name", "string"},
        .handler = {[&](std::string name, std::string s) {
            checkValidNixIdentifier(name);
            autoArgs[name] = 'S' + s;
        }},
    });

    addFlag({
        .longName = "include",
        .shortName = 'I',
        .description = R"(
  Add *path* to the Nix search path. The Nix search path is
  initialized from the colon-separated [`NIX_PATH`](@docroot@/command-ref/env-common.md#env-NIX_PATH) environment
  variable, and is used to look up the location of Nix expressions using [paths](@docroot@/language/values.md#type-path) enclosed in angle
  brackets (i.e., `<nixpkgs>`).

  For instance, passing

  ```
  -I /home/eelco/Dev
  -I /etc/nixos
  ```

  will cause Lix to look for paths relative to `/home/eelco/Dev` and
  `/etc/nixos`, in that order. This is equivalent to setting the
  `NIX_PATH` environment variable to

  ```
  /home/eelco/Dev:/etc/nixos
  ```

  It is also possible to match paths against a prefix. For example,
  passing

  ```
  -I nixpkgs=/home/eelco/Dev/nixpkgs-branch
  -I /etc/nixos
  ```

  will cause Lix to search for `<nixpkgs/path>` in
  `/home/eelco/Dev/nixpkgs-branch/path` and `/etc/nixos/nixpkgs/path`.

  If a path in the Nix search path starts with `http://` or `https://`,
  it is interpreted as the URL of a tarball that will be downloaded and
  unpacked to a temporary location. The tarball must consist of a single
  top-level directory. For example, passing

  ```
  -I nixpkgs=https://github.com/NixOS/nixpkgs/archive/master.tar.gz
  ```

  tells Lix to download and use the current contents of the `master`
  branch in the `nixpkgs` repository.

  The URLs of the tarballs from the official `nixos.org` channels
  (see [the manual page for `nix-channel`](../nix-channel.md)) can be
  abbreviated as `channel:<channel-name>`.  For instance, the
  following two flags are equivalent:

  ```
  -I nixpkgs=channel:nixos-21.05
  -I nixpkgs=https://nixos.org/channels/nixos-21.05/nixexprs.tar.xz
  ```

  You can also fetch source trees using [flake URLs](./nix3-flake.md#url-like-syntax) and add them to the
  search path. For instance,

  ```
  -I nixpkgs=flake:nixpkgs
  ```

  specifies that the prefix `nixpkgs` shall refer to the source tree
  downloaded from the `nixpkgs` entry in the flake registry. Similarly,

  ```
  -I nixpkgs=flake:github:NixOS/nixpkgs/nixos-22.05
  ```

  makes `<nixpkgs>` refer to a particular branch of the
  `NixOS/nixpkgs` repository on GitHub.
  )",
        .category = category,
        .labels = {"path"},
        .handler = {[&](std::string s) {
            searchPath.elements.emplace_back(SearchPath::Elem::parse(s));
        }}
    });

    addFlag({
        .longName = "impure",
        .description = "Allow access to mutable paths and repositories.",
        .category = category,
        .handler = {[&]() {
            evalSettings.pureEval.override(false);
        }},
    });

    addFlag({
        .longName = "override-flake",
        .description = "Override the flake registries, redirecting *original-ref* to *resolved-ref*.",
        .category = category,
        .labels = {"original-ref", "resolved-ref"},
        .handler = {[&](std::string _from, std::string _to) {
            auto from = parseFlakeRef(_from, absPath("."));
            auto to = parseFlakeRef(_to, absPath("."));
            fetchers::Attrs extraAttrs;
            if (to.subdir != "") extraAttrs["dir"] = to.subdir;
            fetchers::overrideRegistry(from.input, to.input, extraAttrs);
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeRef(aio(), completions, aio().blockOn(openStore()), prefix);
        }}
    });

    addFlag({
        .longName = "eval-store",
        .description =
          R"(
            The [URL of the Nix store](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
            to use for evaluation, i.e. to store derivations (`.drv` files) and inputs referenced by them.
          )",
        .category = category,
        .labels = {"store-url"},
        .handler = {&evalStoreUrl},
    });
}

Bindings * MixEvalArgs::getAutoArgs(Evaluator & state)
{
    auto res = state.buildBindings(autoArgs.size());
    for (auto & i : autoArgs) {
        Value v;
        if (i.second[0] == 'E')
            state.evalLazily(
                state.parseExprFromString(i.second.substr(1), CanonPath::fromCwd()), v
            );
        else
            v.mkString(((std::string_view) i.second).substr(1));
        res.insert(state.symbols.create(i.first), v);
    }
    return res.finish();
}

kj::Promise<Result<EvalPaths::PathResult<SourcePath, ThrownError>>>
lookupFileArg(Evaluator & state, std::string_view fileArg)
try {
    if (EvalSettings::isPseudoUrl(fileArg)) {
        auto const url = EvalSettings::resolvePseudoUrl(fileArg);
        auto const downloaded = TRY_AWAIT(fetchers::downloadTarball(
            state.store,
            url,
            /* name */ "source",
            /* locked */ false
        ));
        StorePath const storePath = downloaded.tree.storePath;
        co_return SourcePath(CanonPath(state.store->toRealPath(storePath)));
    } else if (fileArg.starts_with("flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        static constexpr size_t FLAKE_LEN = std::string_view("flake:").size();
        auto flakeRef = parseFlakeRef(std::string(fileArg.substr(FLAKE_LEN)), {}, true, false);
        auto storePath = TRY_AWAIT(TRY_AWAIT(flakeRef.resolve(state.store)).fetchTree(state.store))
                             .first.storePath;
        co_return SourcePath(CanonPath(state.store->toRealPath(storePath)));
    } else if (fileArg.size() > 2 && fileArg.at(0) == '<' && fileArg.at(fileArg.size() - 1) == '>')
    {
        Path p(fileArg.substr(1, fileArg.size() - 2));
        co_return TRY_AWAIT(state.paths.findFile(p));
    } else {
        co_return SourcePath(CanonPath::fromCwd(fileArg));
    }
} catch (...) {
    co_return result::current_exception();
}

}
