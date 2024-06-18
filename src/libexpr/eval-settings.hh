#pragma once
///@file
#include "config.hh"

namespace nix {

struct EvalSettings : Config
{
    EvalSettings();

    static Strings getDefaultNixPath();

    static bool isPseudoUrl(std::string_view s);

    static std::string resolvePseudoUrl(std::string_view url);

    Setting<bool> enableNativeCode{this, false, "allow-unsafe-native-code-during-evaluation", R"(
        Enable built-in functions that allow executing native code.

        In particular, this adds:
        - `builtins.importNative` *path* *symbol*

          Runs function with *symbol* from a dynamic shared object (DSO) at *path*.
          This may be used to add new builtins to the Nix language.
          The procedure must have the following signature:
          ```cpp
          extern "C" typedef void (*ValueInitialiser) (EvalState & state, Value & v);
          ```

        - `builtins.exec` *arguments*

          Execute a program, where *arguments* are specified as a list of strings, and parse its output as a Nix expression.
    )"};

    Setting<Strings> nixPath{
        this, getDefaultNixPath(), "nix-path",
        R"(
          List of directories to be searched for `<...>` file references

          In particular, outside of [pure evaluation mode](#conf-pure-eval), this determines the value of
          [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath).
        )"};

    Setting<std::string> currentSystem{
        this, "", "eval-system",
        R"(
          This option defines
          [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
          in the Nix language if it is set as a non-empty string.
          Otherwise, if it is defined as the empty string (the default), the value of the
          [`system` ](#conf-system)
          configuration setting is used instead.

          Unlike `system`, this setting does not change what kind of derivations can be built locally.
          This is useful for evaluating Nix code on one system to produce derivations to be built on another type of system.
        )"};

    /**
     * Implements the `eval-system` vs `system` defaulting logic
     * described for `eval-system`.
     */
    const std::string & getCurrentSystem();

    Setting<bool> restrictEval{
        this, false, "restrict-eval",
        R"(
          If set to `true`, the Nix evaluator will not allow access to any
          files outside of the Nix search path (as set via the `NIX_PATH`
          environment variable or the `-I` option), or to URIs outside of
          [`allowed-uris`](../command-ref/conf-file.md#conf-allowed-uris).
          The default is `false`.
        )"};

    Setting<bool> pureEval{this, false, "pure-eval",
        R"(
          Pure evaluation mode ensures that the result of Nix expressions is fully determined by explicitly declared inputs, and not influenced by external state:

          - File system and network access is restricted to accesses to immutable data only:
            - Path literals relative to the home directory like `~/lix` are rejected at parse time.
            - Access to absolute paths that did not result from Nix language evaluation is rejected when such paths are given as parameters to builtins like, for example, [`builtins.readFile`](@docroot@/language/builtins.md#builtins-readFile).

              Access is nonetheless allowed to (absolute) paths in the Nix store that are returned by builtins like [`builtins.filterSource`](@docroot@/language/builtins.md#builtins-filterSource), [`builtins.fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball) and similar.
            - Impure fetches such as not specifying a commit ID for `builtins.fetchGit` or not specifying a hash for `builtins.fetchTarball` are rejected.
            - In flakes, access to relative paths outside of the root of the flake's source tree (often, a git repository) is rejected.
          - The evaluator ignores `NIX_PATH`, `-I` and the `nix-path` setting. Thus, [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath) is an empty list.
          - The builtins [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem) and [`builtins.currentTime`](@docroot@/language/builtin-constants.md#builtins-currentTime) are absent from `builtins`.
          - [`builtins.getEnv`](@docroot@/language/builtin-constants.md#builtins-currentSystem) always returns empty string for any variable.
          - [`builtins.storePath`](@docroot@/language/builtins.md#builtins-storePath) throws an error (Lix may change this, tracking issue: <https://git.lix.systems/lix-project/lix/issues/402>)
        )"
        };

    Setting<bool> enableImportFromDerivation{
        this, true, "allow-import-from-derivation",
        R"(
          By default, Lix allows you to `import` from a derivation, allowing
          building at evaluation time. With this option set to false, Lix will
          throw an error when evaluating an expression that uses this feature,
          allowing users to ensure their evaluation will not require any
          builds to take place.
        )"};

    Setting<Strings> allowedUris{this, {}, "allowed-uris",
        R"(
          A list of URI prefixes to which access is allowed in restricted
          evaluation mode. For example, when set to
          `https://github.com/NixOS`, builtin functions such as `fetchGit` are
          allowed to access `https://github.com/NixOS/patchelf.git`.
        )"};


    Setting<bool> traceFunctionCalls{this, false, "trace-function-calls",
        R"(
          If set to `true`, the Nix evaluator will trace every function call.
          Nix will print a log message at the "vomit" level for every function
          entrance and function exit.

              function-trace entered undefined position at 1565795816999559622
              function-trace exited undefined position at 1565795816999581277
              function-trace entered /nix/store/.../example.nix:226:41 at 1565795253249935150
              function-trace exited /nix/store/.../example.nix:226:41 at 1565795253249941684

          The `undefined position` means the function call is a builtin.

          Use the `contrib/stack-collapse.py` script distributed with the Nix
          source code to convert the trace logs in to a format suitable for
          `flamegraph.pl`.
        )"};

    Setting<bool> useEvalCache{this, true, "eval-cache",
        "Whether to use the flake evaluation cache."};

    Setting<bool> ignoreExceptionsDuringTry{this, false, "ignore-try",
        R"(
          If set to true, ignore exceptions inside 'tryEval' calls when evaluating nix expressions in
          debug mode (using the --debugger flag). By default the debugger will pause on all exceptions.
        )"};

    Setting<bool> traceVerbose{this, false, "trace-verbose",
        "Whether `builtins.traceVerbose` should trace its first argument when evaluated."};

    Setting<unsigned int> maxCallDepth{this, 10000, "max-call-depth",
        "The maximum function call depth to allow before erroring."};

    Setting<bool> builtinsTraceDebugger{this, false, "debugger-on-trace",
        R"(
          If set to true and the `--debugger` flag is given,
          [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace) will
          enter the debugger like
          [`builtins.break`](@docroot@/language/builtins.md#builtins-break).

          This is useful for debugging warnings in third-party Nix code.
        )"};

    PathsSetting replOverlays{this, Paths(), "repl-overlays",
        R"(
          A list of files containing Nix expressions that can be used to add
          default bindings to [`nix
          repl`](@docroot@/command-ref/new-cli/nix3-repl.md) sessions.

          Each file is called with three arguments:
          1. An [attribute set](@docroot@/language/values.html#attribute-set)
             containing at least a
             [`currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
             attribute (this is identical to
             [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem),
             except that it's available in
             [`pure-eval`](@docroot@/command-ref/conf-file.html#conf-pure-eval)
             mode).
          2. The top-level bindings produced by the previous `repl-overlays`
             value (or the default top-level bindings).
          3. The final top-level bindings produced by calling all
             `repl-overlays`.

          For example, the following file would alias `pkgs` to
          `legacyPackages.${info.currentSystem}` (if that attribute is defined):

          ```nix
          info: final: prev:
          if prev ? legacyPackages
             && prev.legacyPackages ? ${info.currentSystem}
          then
          {
            pkgs = prev.legacyPackages.${info.currentSystem};
          }
          else
          { }
          ```
        )"};
};

extern EvalSettings evalSettings;

/**
 * Conventionally part of the default nix path in impure mode.
 */
Path getNixDefExpr();

}
