# Lix 2.92 "Bombe glacée" (2025-01-18)


# Lix 2.92.0 (2025-01-18)

## Breaking Changes
- Deprecated language features [fj#437](https://git.lix.systems/lix-project/lix/issues/437) [#861](https://github.com/NixOS/nix/issues/861) [cl/1785](https://gerrit.lix.systems/c/lix/+/1785) [cl/1736](https://gerrit.lix.systems/c/lix/+/1736) [cl/1735](https://gerrit.lix.systems/c/lix/+/1735) [cl/1744](https://gerrit.lix.systems/c/lix/+/1744) [cl/2206](https://gerrit.lix.systems/c/lix/+/2206)

  A system for deprecation (and then the planned removal) of undesired language features has been put into place.
  It is controlled via feature flags much like experimental features, except that the deprecations are enabled default,
  and can be disabled via the flags for backwards compatibility (opt-out with `--extra-deprecated-features` or the Nix configuration file).

  - `url-literals`: **URL literals** have long been obsolete and discouraged of use, and now they are officially deprecated.
    This means that all URLs must be properly put within quotes like all other strings.
  - `rec-set-overrides`: **__overrides** is an old arcane syntax which has not been in use for more than a decade.
    It is soft-deprecated with a warning only, with the plan to turn that into an error in a future release.
  - `ancient-let`: **The old `let` syntax** (`let { body = …; … }`) is soft-deprecated with a warning as well. Use the regular `let … in` instead.
  - `shadow-internal-symbols`: Arithmetic expressions like `5 - 3` internally expand to `__sub 5 3`, where `__sub` maps to a subtraction builtin. Shadowing such a symbols would affect the evaluation of such operations, but in a very inconsistent way, and is therefore deprecated now. **Affected symbols are:** `__sub`, `__mul`, `__div` and `__lessThan`. Note that these symbols may still be used as variable names as long as they do not shadow internal operations, so e.g. `let __sub = x: y: x + y; in __sub 3 5` remains valid code.
    - **Call to action:** If you have any use cases or workflows that depend on being able to override the `__nixPath` and `__findFile` symbols, please reach out to us. We want to eventually deprecate overriding these as well, and need input on how to design a better alternative.

  Many thanks to [piegames](https://git.lix.systems/piegames) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- transfers no longer allow arbitrary url schemas [cl/2106](https://gerrit.lix.systems/c/lix/+/2106)

  Lix no longer allows transfers using arbitrary url schemas. Only `http://`, `https://`, `ftp://`, `ftps://`, and `file://` urls are supported going forward. This affects `builtins.fetchurl`, `<nix/fetchurl.nix>`, transfers to and from binary caches, and all other uses of the internal file transfer code. Flake inputs using multi-protocol schemas (e.g. `git+ssh`) are not affected as those use external utilities to transfer data.

  The `s3://` scheme is not affected at all by this change and continues to work if S3 support is built into Lix.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- Removing the `.` default argument passed to the `nix fmt` formatter [#11438](https://github.com/NixOS/nix/pull/11438) [cl/1902](https://gerrit.lix.systems/c/lix/+/1902)

  The underlying formatter no longer receives the ". " default argument when `nix fmt` is called with no arguments.

  This change was necessary as the formatter wasn't able to distinguish between
  a user wanting to format the current folder with `nix fmt .` or the generic
  `nix fmt`.

  The default behaviour is now the responsibility of the formatter itself, and
  allows tools such as treefmt to format the whole tree instead of only the
  current directory and below.

  This may cause issues with some formatters: nixfmt, nixpkgs-fmt and alejandra currently format stdin when no arguments are passed.

  Here is a small wrapper example that will restore the previous behaviour for such a formatter:

  ```nix
  {
    outputs = { self, nixpkgs, systems }:
      let
        eachSystem = nixpkgs.lib.genAttrs (import systems) (system: nixpkgs.legacyPackages.${system});
      in
      {
        formatter = eachSystem (pkgs:
          pkgs.writeShellScriptBin "formatter" ''
            if [[ $# = 0 ]]; then set -- .; fi
            exec "${pkgs.nixfmt-rfc-style}/bin/nixfmt" "$@"
          '');
      };
  }
  ```

  Many thanks to [zimbatm](https://github.com/zimbatm) for this.

## Features
- Relative and tilde paths in configuration [fj#482](https://git.lix.systems/lix-project/lix/issues/482) [cl/1851](https://gerrit.lix.systems/c/lix/+/1851) [cl/1863](https://gerrit.lix.systems/c/lix/+/1863) [cl/1864](https://gerrit.lix.systems/c/lix/+/1864)

  [Configuration settings](@docroot@/command-ref/conf-file.md) can now refer to
  files with paths relative to the file they're written in or relative to your
  home directory (with `~/`).

  This makes settings like
  [`repl-overlays`](@docroot@/command-ref/conf-file.md#conf-repl-overlays) and
  [`secret-key-files`](@docroot@/command-ref/conf-file.md#conf-repl-overlays)
  much easier to set, especially if you'd like to refer to files in an existing
  dotfiles repo cloned into your home directory.

  If you put `repl-overlays = repl.nix` in your `~/.config/nix/nix.conf`, it'll
  load `~/.config/nix/repl.nix`. Similarly, you can set `repl-overlays =
  ~/.dotfiles/repl.nix` to load a file relative to your home directory.

  Configuration files can also
  [`include`](@docroot@/command-ref/conf-file.md#file-format) paths relative to
  your home directory.

  Only user configuration files (like `$XDG_CONFIG_HOME/nix/nix.conf` or the
  files listed in `$NIX_USER_CONF_FILES`) can use tilde paths relative to your
  home directory. Configuration listed in the `$NIX_CONFIG` environment variable
  may not use relative paths.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.

## Improvements
- Improved error messages for bad attr paths [cl/2277](https://gerrit.lix.systems/c/lix/+/2277) [cl/2280](https://gerrit.lix.systems/c/lix/+/2280)

  Lix now includes much more detail when a bad attribute path is accessed at the command line:

  ```
   » nix eval -f '<nixpkgs>' lixVersions.lix_2_92
  error: attribute 'lix_2_92' in selection path 'lixVersions.lix_2_92' not found
         Did you mean one of lix_2_90 or lix_2_91?
  ```

  After:

  ```
   » nix eval --impure -f '<nixpkgs>' lixVersions.lix_2_92
  error: attribute 'lix_2_92' in selection path 'lixVersions.lix_2_92' not found inside path 'lixVersions', whose contents are: { __unfix__ = «lambda @ /nix/store/hfz1qqd0z8amlgn8qwich1dvkmldik36-source/lib/fixed-points.nix:
  447:7»; buildLix = «thunk»; extend = «thunk»; latest = «thunk»; lix_2_90 = «thunk»; lix_2_91 = «thunk»; override = «thunk»; overrideDerivation = «thunk»; recurseForDerivations = true; stable = «thunk»; }
         Did you mean one of lix_2_90 or lix_2_91?
  ```

  This should avoid some unnecessary trips to the repl or to the debugger by giving some information about the value being selected on that was unexpected.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Small error message improvements [cl/2185](https://gerrit.lix.systems/c/lix/+/2185) [cl/2187](https://gerrit.lix.systems/c/lix/+/2187)

  When an attribute selection fails, the error message now correctly points to the attribute in the chain that failed instead of at the beginning of the entire chain.
  ```diff
   error: attribute 'x' missing
  -       at /pwd/lang/eval-fail-remove.nix:4:3:
  +       at /pwd/lang/eval-fail-remove.nix:4:29:
               3| in
               4|   (removeAttrs attrs ["x"]).x
  -             |   ^
  +             |                             ^
               5|
  ```

  Failed asserts don't print the failed assertion expression anymore in the error message. That code was buggy and the information was redundant anyways, given that the error position already more accurately shows what exactly failed.

  Many thanks to [piegames](https://git.lix.systems/piegames) for this.
- Improvements to interactive flake config [cl/2066](https://gerrit.lix.systems/c/lix/+/2066)

  If `accept-flake-config` is set to `ask` and a `flake.nix` defines `nixConfig`,
  Lix will ask on the CLI which of these settings should be used for the command.

  Now, it's possible to answer with `N` (as opposed to `n` to only reject the setting
  that is asked for) to reject _all untrusted_ entries from the flake's `nixConf`
  section.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.
- `nix --version` now shows details about the installation by default [fj#620](https://git.lix.systems/lix-project/lix/issues/620) [cl/2365](https://gerrit.lix.systems/c/lix/+/2365)

  This happened with `nix-env --version` by default, but due to [oddities around the nix3 CLI's verbosity](https://gerrit.lix.systems/c/lix/+/1370), it used to be `nix --verbose --version`.

  No longer:

  ```
  $ nix --version
  nix (Lix, like Nix) 2.92.0-dev-pre20250117-0d14c2b
  System type: x86_64-linux
  Additional system types: i686-linux, x86_64-v1-linux, x86_64-v2-linux, x86_64-v3-linux
  Features: gc, signed-caches
  System configuration file: /etc/nix/nix.conf
  User configuration files: /home/jade/.config/nix/nix.conf:/etc/xdg/nix/nix.conf
  Store directory: /nix/store
  State directory: /nix/var/nix
  Data directory: /nix/store/rliimcnqkplrqdgm4z6yqclpr6c32wh6-lix-2.92.0-dev-pre20250117-0d14c2b/share
  ```

  Many thanks to [just1602](https://git.lix.systems/just1602) for this.
- `nix repl` correctly tab-completes attribute names that require quotes [cl/1783](https://gerrit.lix.systems/c/lix/+/1783)

  The REPL (`nix repl`) now includes quotes as part of attribute names while completing with `<TAB>`,
  if necessary. For example, attribute names like `"hello@example.com"` or `"hello world"` would
  be suggested without quotes, resulting in invalid syntax.

  Many thanks to [ian-h-chamberlain](https://git.lix.systems/ian-h-chamberlain) for this.
- Reproducibility check builds now report all differing outputs [cl/2069](https://gerrit.lix.systems/c/lix/+/2069)

  `nix-build --check` allows rerunning the build of an already-built derivation to check that it produces the same output again.

  If a multiple-output derivation with impure behaviour is built with `--check`, only the first output would be shown in the resulting error message (and kept for comparison):

  ```
  error: derivation '/nix/store/4spy3nz1661zm15gkybsy1h5f36aliwx-python3.11-test-1.0.0.drv' may not be deterministic: output '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test
  -1.0.0-dist' differs from '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist.check'
  ```

  Now, all differing outputs are kept and reported:
  ```
  error: derivation '4spy3nz1661zm15gkybsy1h5f36aliwx-python3.11-test-1.0.0.drv' may not be deterministic: outputs differ
           output differs: output '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist' differs from '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist.check'
           output differs: output '/nix/store/yl59v08356i841c560alb0zmk7q16klb-python3.11-test-1.0.0' differs from '/nix/store/yl59v08356i841c560alb0zmk7q16klb-python3.11-test-1.0.0.check'
  ```

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.
- Some Lix crashes now produce reporting instructions and a stack trace, then abort [cl/1854](https://gerrit.lix.systems/c/lix/+/1854)

  Lix, being a C++ program, can crash in a few kinds of ways.
  It can obviously do a memory access violation, which will generate a core dump and thus be relatively debuggable.
  But, worse, it could throw an unhandled exception, and, in the past, we would just show the message but not where it comes from, in spite of this always being a bug, since we expect all such errors to be translated to a Lix specific error.
  Now the latter kind of bug should print reporting instructions, a rudimentary stack trace and (depending on system configuration) generate a core dump.

  Sample output:

  ```
  Lix crashed. This is a bug. We would appreciate if you report it along with what caused it at https://git.lix.systems/lix-project/lix/issues with the following information included:

  Exception: std::runtime_error: test exception
  Stack trace:
   0# nix::printStackTrace() in /home/jade/lix/lix3/build/lix/nix/../libutil/liblixutil.so
   1# 0x000073C9862331F2 in /home/jade/lix/lix3/build/lix/nix/../libmain/liblixmain.so
   2# 0x000073C985F2E21A in /nix/store/p44qan69linp3ii0xrviypsw2j4qdcp2-gcc-13.2.0-lib/lib/libstdc++.so.6
   3# 0x000073C985F2E285 in /nix/store/p44qan69linp3ii0xrviypsw2j4qdcp2-gcc-13.2.0-lib/lib/libstdc++.so.6
   4# nix::handleExceptions(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, std::function<void ()>) in /home/jade/lix/lix3/build/lix/nix/../libmain/liblixmain.so
   ...
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Add a `temp-dir` setting to set the temporary directory location [#7731](https://github.com/NixOS/nix/issues/7731) [#8995](https://github.com/NixOS/nix/issues/8995) [fj#112](https://git.lix.systems/lix-project/lix/issues/112) [fj#253](https://git.lix.systems/lix-project/lix/issues/253) [cl/2103](https://gerrit.lix.systems/c/lix/+/2103)

  [`temp-dir`](@docroot@/command-ref/conf-file.md#conf-temp-dir) can now be set in the Nix
  configuration to change the temporary directory. This can be used to relocate all temporary files
  to another filesystem without affecting the `TMPDIR` env var inherited by interactive
  `nix-shell`/`nix shell` shells or `nix run` commands.

  Also on macOS, the `TMPDIR` env var is no longer unset for interactive shells when pointing
  to a per-session `/var/folders/` directory.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.

## Fixes
- Build failures caused by `allowSubstitutes = false` while being the wrong system now produce a decent error [fj#484](https://git.lix.systems/lix-project/lix/issues/484) [cl/1841](https://gerrit.lix.systems/c/lix/+/1841)

  Nix allows derivations to set `allowSubstitutes = false` in order to force them to be built locally without querying substituters for them.
  This is useful for derivations that are very fast to build (especially if they produce large output).
  However, this can shoot you in the foot if the derivation *has* to be substituted such as if the derivation is for another architecture, which is what `--always-allow-substitutes` is for.

  Perhaps such derivations that are known to be impossible to build locally should ignore `allowSubstitutes` (irrespective of remote builders) in the future, but this at least reports the failure and solution directly.

  ```
  $ nix build -f fail.nix
  error: a 'unicornsandrainbows-linux' with features {} is required to build '/nix/store/...-meow.drv', but I am a 'x86_64-linux' with features {...}

         Hint: the failing derivation has allowSubstitutes set to false, forcing it to be built rather than substituted.
         Passing --always-allow-substitutes to force substitution may resolve this failure if the path is available in a substituter.
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- `Alt+Left` and `Alt+Right` go back/forwards by words in `nix repl` [fj#501](https://git.lix.systems/lix-project/lix/issues/501) [cl/1883](https://gerrit.lix.systems/c/lix/+/1883)

  `nix repl` now recognizes `Alt+Left` and `Alt+Right` for navigating by words
  when entering input in `nix repl` on more terminals/platforms.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Ctrl-C stops Nix commands much more reliably and responsively [#7245](https://github.com/NixOS/nix/issues/7245) [fj#393](https://git.lix.systems/lix-project/lix/issues/393) [#11618](https://github.com/NixOS/nix/pull/11618) [cl/2016](https://gerrit.lix.systems/c/lix/+/2016)

  CTRL-C will now stop Nix commands much more reliably and responsively. While
  there are still some cases where a Nix command can be slow or unresponsive
  following a `SIGINT` (please report these as issues!), the vast majority of
  signals will now cause the Nix command to quit quickly and consistently.

  Many thanks to [Robert Hensing](https://github.com/roberth) and [wiggles](https://git.lix.systems/rbt) for this.
- restore backwards-compatibility of `builtins.fetchGit` with Nix 2.3 [#5291](https://github.com/NixOS/nix/issues/5291) [#5128](https://github.com/NixOS/nix/issues/5128)

  Compatibility with `builtins.fetchGit` from Nix 2.3 has been restored as follows:

  * Until now, each `ref` was prefixed with `refs/heads` unless it starts with `refs/` itself.

    Now, this is not done if the `ref` looks like a commit hash.

  * Specifying `builtins.fetchGit { ref = "a-tag"; /* … */ }` was broken because `refs/heads` was appended.

    Now, the fetcher doesn't turn a ref into `refs/heads/ref`, but into `refs/*/ref`. That way,
    the value in `ref` can be either a tag or a branch.

  * The ref resolution happens the same way as in git:

    * If `refs/ref` exists, it's used.
    * If a tag `refs/tags/ref` exists, it's used.
    * If a branch `refs/heads/ref` exists, it's used.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.
- Flakes/restrict-eval no longer allow reading contents of impure paths

  Flakes and `--restrict-eval` now correctly restrict access to paths as intended.
  In prior versions since at least 2.18, `nix eval --raw .#lol` for the following flake didn't throw an error and acted as if `--impure` was passed.

  Thanks to the person who reported this for telling us about it.
  This was handled as a low-severity security bug, but is not a violation of the [documented security model](../installation/multi-user.md) as untrusted Nix code should be assumed to have the privileges of the user running the evaluator.
  To report a security bug, email a report to `security at lix dot systems`.

  ```nix
  {
    inputs = {};
    outputs = {...}: {
      lol = builtins.readFile "${/etc/passwd}";
    };
  }
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- HTTP proxy environment variables are now respected for S3 binary cache stores [fj#433](https://git.lix.systems/lix-project/lix/issues/433) [cl/1788](https://gerrit.lix.systems/c/lix/+/1788)

  Due to "legacy reasons" (according to the AWS C++ SDK docs), the AWS SDK ignores system proxy configuration by default.
  We turned it back on.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Fix potential store corruption with auto-optimise-store [#7273](https://github.com/NixOS/nix/issues/7273) [cl/2100](https://gerrit.lix.systems/c/lix/+/2100)

  Optimising store paths (and other operations involving temporary files) no longer use `random(3)`
  to generate filenames. On darwin systems this was observed to potentially cause store corruption
  when using [`auto-optimise-store`](@docroot@/command-ref/conf-file.md#conf-auto-optimise-store),
  though this corruption was possible on any system whose `random(3)` does not have locking around
  the global state.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.
- Change `nix-build -o ""` to behave like `--no-out-link` [cl/2103](https://gerrit.lix.systems/c/lix/+/2103)

  [`nix-build`](@docroot@/command-ref/nix-build.md) now treats <code>[--out-link](@docroot@/command-ref/nix-build.md#opt-out-link) ''</code>
  the same as [`--no-out-link`](@docroot@/command-ref/nix-build.md#opt-no-out-link). This matches
  [`nix build`](@docroot@/command-ref/new-cli/nix3-build.md) behavior. Previously when building the default output it
  would have resulted in throwing an error saying the current working directory already exists, and when building any
  other output it would have resulted in a symlink starting with a hyphen such as `-doc`, which is a footgun for
  terminal commands.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.
- Ignore broken `/etc/ssl/certs/ca-certificates.crt` symlink [fj#560](https://git.lix.systems/lix-project/lix/issues/560) [cl/2144](https://gerrit.lix.systems/c/lix/+/2144)

  [`ssl-cert-file`](@docroot@/command-ref/conf-file.md#conf-ssl-cert-file) now checks its default
  value for a broken symlink before using it. This fixes a problem on macOS where uninstalling
  nix-darwin may leave behind a broken symlink at `/etc/ssl/certs/ca-certificates.crt` that was
  stopping Lix from using the cert at `/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt`.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.
- `<nix/fetchurl.nix>` now uses TLS verification [#11585](https://github.com/NixOS/nix/pull/11585)

  Previously `<nix/fetchurl.nix>` did not do TLS verification. This was because the Nix sandbox in the past did not have access to TLS certificates, and Nix checks the hash of the fetched file anyway. However, this can expose authentication data from `netrc` and URLs to man-in-the-middle attackers. In addition, Nix now in some cases (such as when using impure derivations) does *not* check the hash. Therefore we have now enabled TLS verification. This means that downloads by `<nix/fetchurl.nix>` will now fail if you're fetching from a HTTPS server that does not have a valid certificate.

  `<nix/fetchurl.nix>` is also known as the builtin derivation builder `builtin:fetchurl`. It's not to be confused with the evaluation-time function `builtins.fetchurl`, which was not affected by this issue.

  Many thanks to [Eelco Dolstra](https://github.com/edolstra) for this.

## Packaging
- readline support removed [cl/1885](https://gerrit.lix.systems/c/lix/+/1885)

  Support for building Lix with [`readline`][readline] instead of
  [`editline`][editline] has been removed. `readline` support hasn't worked for a
  long time (attempting to use it would lead to build errors) and would make Lix
  subject to the GPL if it did work. In the future, we're hoping to replace
  `editline` with [`rustyline`][rustyline] for improved ergonomics in the `nix
  repl`.

  [readline]: https://en.wikipedia.org/wiki/GNU_Readline
  [editline]: https://github.com/troglobit/editline
  [rustyline]: https://github.com/kkawakam/rustyline

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.

## Development
- Includes are now qualified with library name everywhere [cl/2178](https://gerrit.lix.systems/c/lix/+/2178) [cl/2362](https://gerrit.lix.systems/c/lix/+/2362)

  The Lix includes have all been rearranged to be of the form `"lix/libexpr/foo.hh"` instead of `"foo.hh"`.
  This was already supported externally for a migration period, but it is now being applied to all the internal usages within Lix itself.
  The goal of this change is to both clarify where a file is from and to avoid polluting global include paths with things like `config.h` that might conflict with other projects.

  Lix 2.92 removes support for the old `"foo.hh"` include form either internally or externally (that is, via pkg-config for things linking to Lix).

  For other details, see the release notes of Lix 2.90.0, under "Rename all the libraries" in Breaking Changes.

  To fix an external project with sources in `src` which has a separate build directory (such that headers are in `../src` relative to where the compiler is running), use a checkout of Lix 2.91 to run the following:

  ```
  lix_root=$HOME/lix
  (cd $lix_root && nix develop -c 'meson setup build && ninja -C build subprojects/lix-clang-tidy/liblix-clang-tidy.so')
  run-clang-tidy -checks='-*,lix-fixincludes' -load=$lix_root/build/subprojects/lix-clang-tidy/liblix-clang-tidy.so -p build/ -header-filter '\.\./src/.*\.h' -fix src
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- The beginnings of a new pytest-based functional test suite [cl/2036](https://gerrit.lix.systems/c/lix/+/2036) [cl/2037](https://gerrit.lix.systems/c/lix/+/2037)

  The existing integration/functional test suite is based on a large volume of shell scripts.
  This often makes it somewhat challenging to debug at the best of times.
  The goal of the pytest test suite is to make tests have more obvious dependencies on files and to make tests more concise and easier to write, as well as making new testing methods like snapshot testing easy.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Dependency on monolithic coreutils removed [cl/2108](https://gerrit.lix.systems/c/lix/+/2108)

  Previously, the build erroneously depended on a `coreutils` binary, which requires `coreutils` to be built with a specific configuration. This was only used in one test and was not required to be a single binary. This dependency is removed now.

  Many thanks to [Vigress](https://git.lix.systems/vigress8) for this.
- All Lix threads are named [cl/2210](https://gerrit.lix.systems/c/lix/+/2210)

  Lix now sets thread names on all of its secondary threads, which will make debugger usage slightly nicer and easier.

  ```
  (gdb) info thr
    Id   Target Id                    Frame
  * 1    LWP 3719283 "nix-daemon"     0x00007e558587da0f in accept ()
     from target:/nix/store/c10zhkbp6jmyh0xc5kd123ga8yy2p4hk-glibc-2.39-52/lib/libc.so.6
    2    LWP 3719284 "signal handler" 0x00007e55857b2bea in sigtimedwait ()
     from target:/nix/store/c10zhkbp6jmyh0xc5kd123ga8yy2p4hk-glibc-2.39-52/lib/libc.so.6
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Set `X-GitHub-Api-Version` header [fj#255](https://git.lix.systems/lix-project/lix/issues/255) [cl/1925](https://gerrit.lix.systems/c/lix/+/1925)

  Sets the `X-GitHub-Api-Version` header to `2022-11-28` for calls to the
  GitHub API.
  This follows the later version as per
  https://docs.github.com/en/rest/about-the-rest-api/api-versions?apiVersion=2022-11-28.

  This affected the check on whether to use the API versus unauthenticated
  calls as well, given the headers would no longer be empty if the
  authentication token were missing.
  The workaround to this used here is to use a check similar to an existing
  check for the token.

  In the current implementation, headers are (still) similarly sent to
  non-authenticated as well as GitHub on-prem calls.
  For what it's worth, manual curl calls with such a header seemed to
  break nor unauthenticated calls nor ones to the github.com API.

  Many thanks to [kiara](https://github.com/KiaraGrouwstra) for this.

## Miscellany
- Drop support for `xz` and `bzip2` Content-Encoding [cl/2134](https://gerrit.lix.systems/c/lix/+/2134)

  Lix no longer supports the non-standard HTTP Content-Encoding values `xz` and `bzip2`.
  We do not expect this to cause any problems in practice since these encodings *aren't*
  standard, and any server delivering them anyway without being asked to is already well
  and truly set on the path of causing inexplicable client breakages.

  Lix's ability to decompress files compressed with `xz` or `bzip2` is unaffected. We're
  only bringing Lix more in line with the HTTP standard; all post-transfer data handling
  remains as it was before.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
