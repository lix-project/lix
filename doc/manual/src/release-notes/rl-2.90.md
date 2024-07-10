# Lix 2.90 "Vanilla Ice Cream" (2024-07-10)


# Lix 2.90.0 (2024-07-10)

## Breaking Changes
- Deprecate the online flake registries and vendor the default registry [fj#183](https://git.lix.systems/lix-project/lix/issues/183) [fj#110](https://git.lix.systems/lix-project/lix/issues/110) [fj#116](https://git.lix.systems/lix-project/lix/issues/116) [#8953](https://github.com/NixOS/nix/issues/8953) [#9087](https://github.com/NixOS/nix/issues/9087) [cl/1127](https://gerrit.lix.systems/c/lix/+/1127)

  The online flake registry [https://channels.nixos.org/flake-registry.json](https://channels.nixos.org/flake-registry.json) is not pinned in any way,
  and the targets of the indirections can both update or change entirely at any
  point. Furthermore, it is refetched on every use of a flake reference, even if
  there is a local flake reference, and even if you are offline (which breaks).

  For now, we deprecate the (any) online flake registry, and vendor a copy of the
  current online flake registry. This makes it work offline, and ensures that
  it won't change in the future.

  Many thanks to [julia](https://git.lix.systems/midnightveil) for this.
- Enforce syscall filtering and no-new-privileges on Linux [cl/1063](https://gerrit.lix.systems/c/lix/+/1063)

  In order to improve consistency of the build environment, system call filtering and no-new-privileges are now unconditionally enabled on Linux.
  The `filter-syscalls` and `allow-new-privileges` options which could be used to disable these features under some circumstances have been removed.

  In order to support building on architectures without libseccomp support, the option to disable syscall filtering at build time remains.
  However, other uses of this option are heavily discouraged, since it would reduce the security of the sandbox substantially.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.
- Overhaul `nix flake update` and `nix flake lock` UX [#8817](https://github.com/NixOS/nix/pull/8817)

  The interface for creating and updating lock files has been overhauled:

  - [`nix flake lock`](@docroot@/command-ref/new-cli/nix3-flake-lock.md) only creates lock files and adds missing inputs now.
  It will *never* update existing inputs.

  - [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) does the same, but *will* update inputs.
  - Passing no arguments will update all inputs of the current flake, just like it already did.
  - Passing input names as arguments will ensure only those are updated. This replaces the functionality of `nix flake lock --update-input`
  - To operate on a flake outside the current directory, you must now pass `--flake path/to/flake`.

  - The flake-specific flags `--recreate-lock-file` and `--update-input` have been removed from all commands operating on installables.
  They are superceded by `nix flake update`.

  Many thanks to [iFreilicht](https://github.com/iFreilicht), [Lunaphied](https://git.lix.systems/Lunaphied), and [Théophane Hufschmitt](https://github.com/thufschmitt) for this.
- `nix profile` now allows referring to elements by human-readable name, and no longer accepts indices [#8678](https://github.com/NixOS/nix/pull/8678) [cl/978](https://gerrit.lix.systems/c/lix/+/978) [cl/980](https://gerrit.lix.systems/c/lix/+/980)

  [`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) now uses names to refer to installed packages when running [`list`](@docroot@/command-ref/new-cli/nix3-profile-list.md), [`remove`](@docroot@/command-ref/new-cli/nix3-profile-remove.md) or [`upgrade`](@docroot@/command-ref/new-cli/nix3-profile-upgrade.md) as opposed to indices. Indices have been removed. Profile element names are generated when a package is installed and remain the same until the package is removed.

  **Warning**: The `manifest.nix` file used to record the contents of profiles has changed. Lix will automatically upgrade profiles to the new version when you modify the profile. After that, the profile can no longer be used by older versions of Lix.

  Many thanks to [iFreilicht](https://github.com/iFreilicht), [Qyriad](https://git.lix.systems/Qyriad), and [Eelco Dolstra](https://github.com/edolstra) for this.
- `builtins.nixVersion` and `builtins.langVersion` return fixed values [cl/558](https://gerrit.lix.systems/c/lix/+/558) [cl/1144](https://gerrit.lix.systems/c/lix/+/1144)

  `builtins.nixVersion` now returns a fixed value `"2.18.3-lix"`.

  `builtins.langVersion` returns a fixed value `6`, matching CppNix 2.18.

  This prevents feature detection assuming that features that exist in Nix
  post-Lix-branch-off might exist, even though the Lix version is greater than
  the Nix version.

  In the future, check for builtins for feature detection. If a feature cannot be
  detected by *those* means, please file a Lix bug.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Rename all the libraries nixexpr, nixstore, etc to lixexpr, lixstore, etc

  The Lix C++ API libraries have had the following changes:
  - Includes moved from `include/nix/` to `include/lix/`
  - `pkg-config` files renamed from `nix-expr` to `lix-expr` and so on.
  - Libraries renamed from `libnixexpr.so` to `liblixexpr.so` and so on.

  There are other changes between Nix 2.18 and Lix, since these APIs are not
  stable. However, this change in particular is a deliberate compatibility break
  to force downstreams linking to Lix to specifically handle Lix and avoid Lix
  accidentally getting ensnared in compatibility code for newer CppNix.

  Migration path:

  - expr.hh      -> lix/libexpr/expr.hh
  - nix/config.h -> lix/config.h

  To apply this migration automatically, remove all `<nix/>` from includes, so `#include <nix/expr.hh>` -> `#include <expr.hh>`.
  Then, the correct paths will be resolved from the tangled mess, and the clang-tidy automated fix will work.

  Then run the following for out of tree projects (header filter is set to only fix instances in headers in `../src` relative to the compiler's working directory, as would be the case in nix-eval-jobs or other things built with meson, e.g.):

  ```console
  lix_root=$HOME/lix
  (cd $lix_root/clang-tidy && nix develop -c 'meson setup build && ninja -C build')
  run-clang-tidy -checks='-*,lix-fixincludes' -load=$lix_root/clang-tidy/build/liblix-clang-tidy.so -p build/ -header-filter '\.\./src/.*\.h' -fix src
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.

## Features
- Experimental REPL support for documentation comments using `:doc` [cl/564](https://gerrit.lix.systems/c/lix/+/564)

  Using `:doc` in the REPL now supports showing documentation comments when defined on a function.

  Previously this was only able to document builtins, however it now will show comments defined on a lambda as well.

  This support is experimental and relies on an embedded version of [nix-doc](https://github.com/lf-/nix-doc).

  The logic also supports limited Markdown formatting of doccomments and should easily support any [RFC 145](https://github.com/NixOS/rfcs/blob/master/rfcs/0145-doc-strings.md)
  compatible documentation comments in addition to simple commented documentation.

  Many thanks to [Lunaphied](https://git.lix.systems/Lunaphied) and [jade](https://git.lix.systems/jade) for this.
- Add `repl-overlays` option [#10203](https://github.com/NixOS/nix/pull/10203) [cl/504](https://gerrit.lix.systems/c/lix/+/504)

  A `repl-overlays` option has been added, which specifies files that can overlay
  and modify the top-level bindings in `nix repl`. For example, with the
  following contents in `~/.config/nix/repl.nix`:

  ```nix
  info: final: prev: let
    optionalAttrs = predicate: attrs:
      if predicate
      then attrs
      else {};
  in
    optionalAttrs (prev ? legacyPackages && prev.legacyPackages ? ${info.currentSystem})
    {
      pkgs = prev.legacyPackages.${info.currentSystem};
    }
  ```

  We can run `nix repl` and use `pkgs` to refer to `legacyPackages.${currentSystem}`:

  ```ShellSession
  $ nix repl --repl-overlays ~/.config/nix/repl.nix nixpkgs
  Lix 2.90.0
  Type :? for help.
  Loading installable 'flake:nixpkgs#'...
  Added 5 variables.
  Loading 'repl-overlays'...
  Added 6 variables.
  nix-repl> pkgs.bash
  «derivation /nix/store/g08b5vkwwh0j8ic9rkmd8mpj878rk62z-bash-5.2p26.drv»
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Add a builtin `addDrvOutputDependencies` [#7910](https://github.com/NixOS/nix/issues/7910) [#9216](https://github.com/NixOS/nix/pull/9216)

  This builtin allows taking a `drvPath`-like string and turning it into a string
  with context such that, when it lands in a derivation, it will create
  dependencies on *all the outputs* in its closure (!). Although `drvPath` does this
  today, this builtin starts forming a path to migrate to making `drvPath` have a
  more normal and less surprising string context behaviour (see linked issue and
  PR for more details).

  Many thanks to [John Ericson](https://github.com/ericson2314) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Enter the `--debugger` when `builtins.trace` is called if `debugger-on-trace` is set [#9914](https://github.com/NixOS/nix/pull/9914)

  If the `debugger-on-trace` option is set and `--debugger` is given,
  `builtins.trace` calls will behave similarly to `builtins.break` and will enter
  the debug REPL. This is useful for determining where warnings are being emitted
  from.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Add an option `enable-core-dumps` that enables core dumps from builds [cl/1088](https://gerrit.lix.systems/c/lix/+/1088)

  In the past, Lix disabled core dumps by setting the soft `RLIMIT_CORE` to 0
  unconditionally. Although this rlimit could be altered from the builder since
  it is just the soft limit, this was kind of annoying to do. By passing
  `--option enable-core-dumps true` to an offending build, one can now cause the
  core dumps to be handled by the system in the normal way (winding up in
  `coredumpctl`, say, on Linux).

  Many thanks to [julia](https://git.lix.systems/midnightveil) for this.
- Add new `eval-system` setting [#4093](https://github.com/NixOS/nix/pull/4093)

  Add a new `eval-system` option.
  Unlike `system`, it just overrides the value of `builtins.currentSystem`.
  This is more useful than overriding `system`, because you can build these derivations on remote builders which can work on the given system.
  In contrast, `system` also effects scheduling which will cause Lix to build those derivations locally even if that doesn't make sense.

  `eval-system` only takes effect if it is non-empty.
  If empty (the default) `system` is used as before, so there is no breakage.

  Many thanks to [matthewbauer](https://github.com/matthewbauer) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- add `--store-path` argument to `nix upgrade-nix`, to manually specify the Nix to upgrade to [cl/953](https://gerrit.lix.systems/c/lix/+/953)

  `nix upgrade-nix` by default downloads a manifest to find the new Nix version to upgrade to, but now you can specify `--store-path` to upgrade Nix to an arbitrary version from the Nix store.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

## Improvements
- `nix flake check` logs the checks [#8882](https://github.com/NixOS/nix/issues/8882) [#8893](https://github.com/NixOS/nix/pull/8893) [cl/259](https://gerrit.lix.systems/c/lix/+/259) [cl/260](https://gerrit.lix.systems/c/lix/+/260) [cl/261](https://gerrit.lix.systems/c/lix/+/261) [cl/262](https://gerrit.lix.systems/c/lix/+/262)

  `nix flake check` now logs the checks it runs and the derivations it evaluates:

  ```
  $ nix flake check -v
  evaluating flake...
  checking flake output 'checks'...
  checking derivation 'checks.aarch64-darwin.ghciwatch-tests'...
  derivation evaluated to /nix/store/nh7dlvsrhds4cxl91mvgj4h5cbq6skmq-ghciwatch-test-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-clippy'...
  derivation evaluated to /nix/store/9cb5a6wmp6kf6hidqw9wphidvb8bshym-ghciwatch-clippy-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-doc'...
  derivation evaluated to /nix/store/8brdd3jbawfszpbs7vdpsrhy80as1il8-ghciwatch-doc-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-fmt'...
  derivation evaluated to /nix/store/wjhs0l1njl5pyji53xlmfjrlya0wmz8p-ghciwatch-fmt-0.3.0.drv
  checking derivation 'checks.aarch64-darwin.ghciwatch-audit'...
  derivation evaluated to /nix/store/z0mps8dyj2ds7c0fn0819y5h5611033z-ghciwatch-audit-0.3.0.drv
  checking flake output 'packages'...
  checking derivation 'packages.aarch64-darwin.default'...
  derivation evaluated to /nix/store/41abbdyglw5x9vcsvd89xan3ydjf8d7r-ghciwatch-0.3.0.drv
  checking flake output 'apps'...
  checking flake output 'devShells'...
  checking derivation 'devShells.aarch64-darwin.default'...
  derivation evaluated to /nix/store/bc935gz7dylzmcpdb5cczr8gngv8pmdb-nix-shell.drv
  running 5 flake checks...
  warning: The check omitted these incompatible systems: aarch64-linux, x86_64-darwin, x86_64-linux
  Use '--all-systems' to check all.
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt), [Raito Bezarius](https://git.lix.systems/raito), and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Add an option `always-allow-substitutes` to ignore `allowSubstitutes` in derivations [#8047](https://github.com/NixOS/nix/pull/8047)

  You can set this setting to force a system to always allow substituting even
  trivial derivations like `pkgs.writeText`. This is useful for
  [`nix-fast-build --skip-cached`][skip-cached] and similar to be able to also
  ignore trivial derivations.

  [skip-cached]: https://github.com/Mic92/nix-fast-build?tab=readme-ov-file#avoiding-redundant-package-downloads

  Many thanks to [lovesegfault](https://github.com/lovesegfault) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Concise error printing in `nix repl` [#9928](https://github.com/NixOS/nix/pull/9928) [cl/811](https://gerrit.lix.systems/c/lix/+/811)

  Previously, if an element of a list or attribute set threw an error while
  evaluating, `nix repl` would print the entire error (including source location
  information) inline. This output was clumsy and difficult to parse:

  ```
  nix-repl> { err = builtins.throw "uh oh!"; }
  { err = «error:
         … while calling the 'throw' builtin
           at «string»:1:9:
              1| { err = builtins.throw "uh oh!"; }
               |         ^

         error: uh oh!»; }
  ```

  Now, only the error message is displayed, making the output much more readable.
  ```
  nix-repl> { err = builtins.throw "uh oh!"; }
  { err = «error: uh oh!»; }
  ```

  However, if the whole expression being evaluated throws an error, source
  locations and (if applicable) a stack trace are printed, just like you'd expect:

  ```
  nix-repl> builtins.throw "uh oh!"
  error:
         … while calling the 'throw' builtin
           at «string»:1:1:
              1| builtins.throw "uh oh!"
               | ^

         error: uh oh!
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Show all FOD errors with `nix build --keep-going` [cl/1108](https://gerrit.lix.systems/c/lix/+/1108)

  `nix build --keep-going` now behaves consistently with `nix-build --keep-going`. This means
  that if e.g. multiple FODs fail to build, all hash mismatches are displayed.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.
- Duplicate attribute reports are more accurate [cl/557](https://gerrit.lix.systems/c/lix/+/557)

  Duplicate attribute errors are now more accurate, showing the path at which an error was detected rather than the full, possibly longer, path that caused the error.
  Error reports are now
  ```ShellSession
  $ nix eval --expr '{ a.b = 1; a.b.c.d = 1; }'
  error: attribute 'a.b' already defined at «string»:1:3
         at «string»:1:12:
              1| { a.b = 1; a.b.c.d = 1;
               |            ^
  ```
  instead of
  ```ShellSession
  $ nix eval --expr '{ a.b = 1; a.b.c.d = 1; }'
  error: attribute 'a.b.c.d' already defined at «string»:1:3
         at «string»:1:12:
              1| { a.b = 1; a.b.c.d = 1;
               |            ^
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- Reduce eval memory usage and wall time [#9658](https://github.com/NixOS/nix/pull/9658) [cl/207](https://gerrit.lix.systems/c/lix/+/207)

  Reduce the size of the `Env` struct used in the evaluator by a pointer, or 8 bytes on most modern machines.
  This reduces memory usage during eval by around 2% and wall time by around 3%.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- Warn on unknown settings anywhere in the command line [#10701](https://github.com/NixOS/nix/pull/10701)

  All `nix` commands will now properly warn when an unknown option is specified anywhere in the command line.

  Before:

  ```console
  $ nix-instantiate --option foobar baz --expr '{}'
  warning: unknown setting 'foobar'
  $ nix-instantiate '{}' --option foobar baz --expr
  $ nix eval --expr '{}' --option foobar baz
  { }
  ```

  After:

  ```console
  $ nix-instantiate --option foobar baz --expr '{}'
  warning: unknown setting 'foobar'
  $ nix-instantiate '{}' --option foobar baz --expr
  warning: unknown setting 'foobar'
  $ nix eval --expr '{}' --option foobar baz
  warning: unknown setting 'foobar'
  { }
  ```

  Many thanks to [Cole Helbling](https://github.com/cole-h) for this.
- Nested debuggers are no longer supported [#9920](https://github.com/NixOS/nix/pull/9920)

  Previously, evaluating an expression that throws an error in the debugger would
  enter a second, nested debugger:

  ```
  nix-repl> builtins.throw "what"
  error: what


  Starting REPL to allow you to inspect the current state of the evaluator.

  Welcome to Nix 2.18.1. Type :? for help.

  nix-repl>
  ```

  Now, it just prints the error message like `nix repl`:

  ```
  nix-repl> builtins.throw "what"
  error:
         … while calling the 'throw' builtin
           at «string»:1:1:
              1| builtins.throw "what"
               | ^

         error: what
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Find GC roots using libproc on Darwin [cl/723](https://gerrit.lix.systems/c/lix/+/723)

  Previously, the garbage collector found runtime roots on Darwin by shelling out to `lsof -n -w -F n` then parsing the result. The version of `lsof` packaged in Nixpkgs is very slow on Darwin, so Lix now uses `libproc` directly to speed up GC root discovery, in some tests taking 250ms now instead of 40s.

  Many thanks to [Artemis Tosini](https://git.lix.systems/artemist) for this.
- Increase default stack size on macOS [#9860](https://github.com/NixOS/nix/pull/9860)

  Increase the default stack size on macOS to the same value as on Linux, subject to system restrictions to maximum stack size.
  This should reduce the number of stack overflow crashes on macOS when evaluating Nix code with deep call stacks.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Show more log context for failed builds [#9670](https://github.com/NixOS/nix/pull/9670)

  Show 25 lines of log tail instead of 10 for failed builds.
  This increases the chances of having useful information in the shown logs.

  Many thanks to [DavHau](https://github.com/DavHau) for this.
- rename 'nix show-config' to 'nix config show' [#7672](https://github.com/NixOS/nix/issues/7672) [#9477](https://github.com/NixOS/nix/pull/9477) [cl/993](https://gerrit.lix.systems/c/lix/+/993)

  `nix show-config` was renamed to `nix config show` to be more consistent with the rest of the command-line interface.

  Running `nix show-config` will now print a deprecation warning saying to use `nix config show` instead.

  Many thanks to [Théophane Hufschmitt](https://github.com/thufschmitt) and [ma27](https://git.lix.systems/ma27) for this.
- Print derivation paths in `nix eval` [cl/446](https://gerrit.lix.systems/c/lix/+/446)

  `nix eval` previously printed derivations as attribute sets, so commands that print derivations (e.g. `nix eval nixpkgs#bash`) would infinitely loop and segfault.
  It now prints the `.drv` path the derivation generates instead.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Add an option `--unpack` to unpack archives in `nix store prefetch-file` [#9805](https://github.com/NixOS/nix/pull/9805) [cl/224](https://gerrit.lix.systems/c/lix/+/224)

  It is now possible to fetch an archive then NAR-hash it (as in, hash it in the
  same manner as `builtins.fetchTarball` or fixed-output derivations with
  recursive hash type) in one command.

  Example:

  ```
  ~ » nix store prefetch-file --name source --unpack https://git.lix.systems/lix-project/lix/archive/2.90-beta.1.tar.gz
  Downloaded 'https://git.lix.systems/lix-project/lix/archive/2.90-beta.1.tar.gz' to '/nix/store/yvfqnq52ryjc3janw02ziv7kr6gd0cs1-source' (hash 'sha256-REWlo2RYHfJkxnmZTEJu3Cd/2VM+wjjpPy7Xi4BdDTQ=').
  ```

  Many thanks to [yshui](https://github.com/yshui) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- REPL printing improvements [#9931](https://github.com/NixOS/nix/pull/9931) [#10208](https://github.com/NixOS/nix/pull/10208) [cl/375](https://gerrit.lix.systems/c/lix/+/375) [cl/492](https://gerrit.lix.systems/c/lix/+/492)

  The REPL printer has been improved to do the following:
  - If a string is passed to `:print`, it is printed literally to the screen
  - Structures will be printed as multiple lines when necessary

  Before:

  ```
  nix-repl> { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
  { attrs = { ... }; list = [ ... ]; list' = [ ... ]; }

  nix-repl> :p { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
  { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }

  nix-repl> :p "meow"
  "meow"
  ```

  After:

  ```
  nix-repl> { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
  {
    attrs = { ... };
    list = [ ... ];
    list' = [ ... ];
  }

  nix-repl> :p { attrs = { a = { b = { c = { }; }; }; }; list = [ 1 ]; list' = [ 1 2 3 ]; }
  {
    attrs = {
      a = {
        b = {
          c = { };
        };
      };
    };
    list = [ 1 ];
    list' = [
      1
      2
      3
    ];
  }

  nix-repl> :p "meow"
  meow
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Coercion errors include the failing value [#561](https://github.com/NixOS/nix/issues/561) [#9754](https://github.com/NixOS/nix/pull/9754)

  The `error: cannot coerce a <TYPE> to a string` message now includes the value
  which caused the error.

  Before:

  ```
         error: cannot coerce a set to a string
  ```

  After:

  ```
         error: cannot coerce a set to a string: { aesSupport = «thunk»;
         avx2Support = «thunk»; avx512Support = «thunk»; avxSupport = «thunk»;
         canExecute = «thunk»; config = «thunk»; darwinArch = «thunk»; darwinMinVersion
         = «thunk»; darwinMinVersionVariable = «thunk»; darwinPlatform = «thunk»; «84
         attributes elided»}
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- New-cli flake commands that expect derivations now print the failing value and its type [cl/1177](https://gerrit.lix.systems/c/lix/+/1177)

  In errors like `flake output attribute 'legacyPackages.x86_64-linux.lib' is not a derivation or path`, the message now includes the failing value and type.

  Before:

  ```
      error: flake output attribute 'nixosConfigurations.yuki.config' is not a derivation or path
  ````

  After:

  ```
      error: expected flake output attribute 'nixosConfigurations.yuki.config' to be a derivation or path but found a set: { appstream = «thunk»; assertions = «thunk»; boot = { bcache = «thunk»; binfmt = «thunk»; binfmtMiscRegistrations = «thunk»; blacklistedKernelModules = «thunk»; bootMount = «thunk»; bootspec = «thunk»; cleanTmpDir = «thunk»; consoleLogLevel = «thunk»; «43 attributes elided» }; «48 attributes elided» }
  ```

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- Type errors include the failing value [#561](https://github.com/NixOS/nix/issues/561) [#9753](https://github.com/NixOS/nix/pull/9753)

  In errors like `value is an integer while a list was expected`, the message now
  includes the failing value.

  Before:

  ```
         error: value is a set while a string was expected
  ```

  After:

  ```
         error: expected a string but found a set: { ghc810 = «thunk»;
         ghc8102Binary = «thunk»; ghc8107 = «thunk»; ghc8107Binary = «thunk»;
         ghc865Binary = «thunk»; ghc90 = «thunk»; ghc902 = «thunk»; ghc92 = «thunk»;
         ghc924Binary = «thunk»; ghc925 = «thunk»;  «17 attributes elided»}
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Visual clutter in `--debugger` is reduced [#9919](https://github.com/NixOS/nix/pull/9919)

  Before:
  ```
  info: breakpoint reached


  Starting REPL to allow you to inspect the current state of the evaluator.

  Welcome to Nix 2.20.0pre20231222_dirty. Type :? for help.

  nix-repl> :continue
  error: uh oh


  Starting REPL to allow you to inspect the current state of the evaluator.

  Welcome to Nix 2.20.0pre20231222_dirty. Type :? for help.

  nix-repl>
  ```

  After:

  ```
  info: breakpoint reached

  Nix 2.20.0pre20231222_dirty debugger
  Type :? for help.
  nix-repl> :continue
  error: uh oh

  nix-repl>
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- REPL now supports CTRL+Z to suspend

  Editline is now built with SIGTSTP support, so now typing CTRL+Z in the REPL will suspend the REPL and allow it to be resumed later or backgrounded.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- Allow single quotes in nix-shell shebangs [#8470](https://github.com/NixOS/nix/pull/8470)

  Example:

  ```bash
  #! /usr/bin/env nix-shell
  #! nix-shell -i bash --packages 'terraform.withPlugins (plugins: [ plugins.openstack ])'
  ```

  Many thanks to [ncfavier](https://github.com/ncfavier) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- reintroduce shortened `-E` form for `--expr` to new CLI [cl/605](https://gerrit.lix.systems/c/lix/+/605)

  In the old CLI, it was possible to supply a shorter `-E` flag instead of fully
  specifying `--expr` every time you wished to provide an expression that would
  be evaluated to produce the given command's input. This was retained for the
  `--file` flag when the new CLI utilities were written with `-f`, but `-E` was
  dropped.

  We now restore the `-E` short form for better UX. This is most useful for
  `nix eval` but most any command that takes an Installable argument should benefit
  from it as well.

  Many thanks to [Lunaphied](https://git.lix.systems/Lunaphied) for this.
- Source locations are printed more consistently in errors [#561](https://github.com/NixOS/nix/issues/561) [#9555](https://github.com/NixOS/nix/pull/9555)

  Source location information is now included in error messages more
  consistently. Given this code:

  ```nix
  let
    attr = {foo = "bar";};
    key = {};
  in
    attr.${key}
  ```

  Previously, Nix would show this unhelpful message when attempting to evaluate
  it:

  ```
  error:
         … while evaluating an attribute name

         error: value is a set while a string was expected
  ```

  Now, the error message displays where the problematic value was found:

  ```
  error:
         … while evaluating an attribute name

           at bad.nix:4:11:

              3|   key = {};
              4| in attr.${key}
               |           ^
              5|

         error: expected a string but found a set: { }
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Some stack overflow segfaults are fixed [#9616](https://github.com/NixOS/nix/issues/9616) [#9617](https://github.com/NixOS/nix/pull/9617) [cl/205](https://gerrit.lix.systems/c/lix/+/205)

  The number of nested function calls has been restricted, to detect and report
  infinite function call recursions. The default maximum call depth is 10,000 and
  can be set with [the `max-call-depth`
  option](@docroot@/command-ref/conf-file.md#conf-max-call-depth).

  This fixes segfaults or the following unhelpful error message in many cases:

      error: stack overflow (possible infinite recursion)

  Before:

  ```
  $ nix-instantiate --eval --expr '(x: x x) (x: x x)'
  Segmentation fault: 11
  ```

  After:

  ```
  $ nix-instantiate --eval --expr '(x: x x) (x: x x)'
  error: stack overflow

         at «string»:1:14:
              1| (x: x x) (x: x x)
               |              ^
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Warn about ignored client settings [cl/1026](https://gerrit.lix.systems/c/lix/+/1026)

  Emit a warning for every client-provided setting the daemon ignores because the requesting client is not run by a trusted user.
  Previously this was only a debug message.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Better error reporting for `with` expressions [#9658](https://github.com/NixOS/nix/pull/9658) [cl/207](https://gerrit.lix.systems/c/lix/+/207)

  `with` expressions using non-attrset values to resolve variables are now reported with proper positions.

  Previously an incorrect `with` expression would report no position at all, making it hard to determine where the error originated:

  ```
  nix-repl> with 1; a
  error:
         … <borked>

           at «none»:0: (source not available)

         error: value is an integer while a set was expected
  ```

  Now position information is preserved and reported as with most other errors:

  ```
  nix-repl> with 1; a
  error:
         … while evaluating the first subexpression of a with expression
           at «string»:1:1:
              1| with 1; a
               | ^

         error: expected a set but found an integer: 1
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

## Fixes
- Fix nested flake input `follows` [#6621](https://github.com/NixOS/nix/pull/6621) [cl/994](https://gerrit.lix.systems/c/lix/+/994)

  Previously nested-input overrides were ignored; that is, the following did not
  override anything, in spite of the `nix3-flake` manual documenting it working:

  ```
  {
    inputs = {
      foo.url = "github:bar/foo";
      foo.inputs.bar.inputs.nixpkgs = "nixpkgs";
    };
  }
  ```

  This is useful to avoid the 1000 instances of nixpkgs problem without having
  each flake in the dependency tree to expose all of its transitive dependencies
  for modification.

  Many thanks to [Kha](https://github.com/Kha) and [ma27](https://git.lix.systems/ma27) for this.
- Fix CVE-2024-27297 (GHSA-2ffj-w4mj-pg37) [cl/266](https://gerrit.lix.systems/c/lix/+/266)

  Since Lix fixed-output derivations run in the host network namespace (which we
  wish to change in the future, see
  [lix#285](https://git.lix.systems/lix-project/lix/issues/285)), they may open
  abstract-namespace Unix sockets to each other and to programs on the host. Lix
  contained a now-fixed time-of-check/time-of-use vulnerability where one
  derivation could send writable handles to files in their final location in the
  store to another over an abstract-namespace Unix socket, exit, then the other
  derivation could wait for Lix to hash the paths and overwrite them.

  The impact of this vulnerability is that two malicious fixed-output derivations
  could create a poisoned path for the sources to Bash or similarly important
  software containing a backdoor, leading to local privilege execution.

  CppNix advisory: https://github.com/NixOS/nix/security/advisories/GHSA-2ffj-w4mj-pg37

  Many thanks to [puck](https://git.lix.systems/puck), [jade](https://git.lix.systems/jade), [Théophane Hufschmitt](https://github.com/thufschmitt), [Tom Bereknyei](https://github.com/tomberek), and [Valentin Gagarin](https://github.com/fricklerhandwerk) for this.
- `--debugger` can now access bindings from `let` expressions [#8827](https://github.com/NixOS/nix/issues/8827) [#9918](https://github.com/NixOS/nix/pull/9918)

  Breakpoints and errors in the bindings of a `let` expression can now access
  those bindings in the debugger. Previously, only the body of `let` expressions
  could access those bindings.

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Fix handling of truncated `.drv` files. [#9673](https://github.com/NixOS/nix/pull/9673)

  Previously a `.drv` that was truncated in the middle of a string would case nix to enter an infinite loop, eventually exhausting all memory and crashing.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- The `--debugger` will start more reliably in `let` expressions and function calls [#6649](https://github.com/NixOS/nix/issues/6649) [#9917](https://github.com/NixOS/nix/pull/9917)

  Previously, if you attempted to evaluate this file with the debugger:

  ```nix
  let
    a = builtins.trace "before inner break" (
      builtins.break "hello"
    );
    b = builtins.trace "before outer break" (
      builtins.break a
    );
  in
    b
  ```

  Lix would correctly enter the debugger at `builtins.break a`, but if you asked
  it to `:continue`, it would skip over the `builtins.break "hello"` expression
  entirely.

  Now, Lix will correctly enter the debugger at both breakpoints.

  Many thanks to [wiggles](https://git.lix.systems/rbt) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Creating setuid/setgid binaries with fchmodat2 is now prohibited by the build sandbox [#10501](https://github.com/NixOS/nix/pull/10501)

  The build sandbox blocks any attempt to create setuid/setgid binaries, but didn't check
  for the use of the `fchmodat2` syscall which was introduced in Linux 6.6 and is used by
  glibc >=2.39. This is fixed now.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.
- consistent order of lambda formals in printed expressions [#9874](https://github.com/NixOS/nix/pull/9874)

  Always print lambda formals in lexicographic order rather than the internal, creation-time based symbol order.
  This makes printed formals independent of the context they appear in.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- fix duplicate attribute error positions for `inherit` [#9874](https://github.com/NixOS/nix/pull/9874)

  When an inherit caused a duplicate attribute error, the position of the error was not reported correctly, placing the error with the inherit itself or at the start of the bindings block instead of the offending attribute name.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- `inherit (x) ...` evaluates `x` only once [#9847](https://github.com/NixOS/nix/pull/9847)

  `inherit (x) a b ...` now evaluates the expression `x` only once for all inherited attributes rather than once for each inherited attribute.
  This does not usually have a measurable impact, but side-effects (such as `builtins.trace`) would be duplicated and expensive expressions (such as derivations) could cause a measurable slowdown.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
- Store paths are allowed to start with `.` [#912](https://github.com/NixOS/nix/issues/912) [#9867](https://github.com/NixOS/nix/pull/9867) [#9091](https://github.com/NixOS/nix/pull/9091) [#9095](https://github.com/NixOS/nix/pull/9095) [#9120](https://github.com/NixOS/nix/pull/9120) [#9121](https://github.com/NixOS/nix/pull/9121) [#9122](https://github.com/NixOS/nix/pull/9122) [#9130](https://github.com/NixOS/nix/pull/9130) [#9219](https://github.com/NixOS/nix/pull/9219) [#9224](https://github.com/NixOS/nix/pull/9224)

  Leading periods were allowed by accident in Nix 2.4. The Nix team has considered this to be a bug, but this behavior has since been relied on by users, leading to unnecessary difficulties.
  From now on, leading periods are officially, definitively supported. The names `.` and `..` are disallowed, as well as those starting with `.-` or `..-`.

  Nix versions that denied leading periods are documented [in the issue](https://github.com/NixOS/nix/issues/912#issuecomment-1919583286).

  Many thanks to [Robert Hensing](https://github.com/roberth) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- Fix `nix-env --query --drv-path --json` [#9257](https://github.com/NixOS/nix/pull/9257)

  Fixed a bug where `nix-env --query` ignored `--drv-path` when `--json` was set.

  Many thanks to [Artturin](https://github.com/Artturin) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- re-evaluate cached evaluation errors [cl/771](https://gerrit.lix.systems/c/lix/+/771)

  "cached failure of [expr]" errors have been removed: expressions already in the
  eval cache as a failure will now simply be re-evaluated, removing the need to
  set `--no-eval-cache` or similar to see the error.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- Interrupting builds in the REPL works more than once [cl/1097](https://gerrit.lix.systems/c/lix/+/1097)

  Builds in the REPL can be interrupted by pressing Ctrl+C.
  Previously, this only worked once per REPL session; further attempts would be ignored.
  This issue is now fixed, so that builds can be canceled consistently.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.
- In the debugger, `while evaluating the attribute` errors now include position information [#9915](https://github.com/NixOS/nix/pull/9915)

  Before:

  ```
  0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
  0x600001522598
  ```

  After:

  ```
  0: while evaluating the attribute 'python311.pythonForBuild.pkgs'
  /nix/store/hg65h51xnp74ikahns9hyf3py5mlbbqq-source/overrides/default.nix:132:27

     131|
     132|       bootstrappingBase = pkgs.${self.python.pythonAttr}.pythonForBuild.pkgs;
        |                           ^
     133|     in
  ```

  Many thanks to [wiggles](https://git.lix.systems/rbt) for this.
- Include phase reporting in log file for ssh-ng builds [#9280](https://github.com/NixOS/nix/pull/9280)

  Store phase information of remote builds run via `ssh-ng` remotes in the local log file, matching logging behavior of local builds.

  Many thanks to [r-vdp](https://github.com/r-vdp) for this.
- Fix `ssh-ng://` remotes not respecting `--substitute-on-destination` [#9600](https://github.com/NixOS/nix/pull/9600)

  `nix copy ssh-ng://` now respects `--substitute-on-destination`, as does `nix-copy-closure` and other commands that operate on remote `ssh-ng` stores.
  Previously this was always set by `builders-use-substitutes` setting.

  Many thanks to [SharzyL](https://github.com/SharzyL) for this.
- using `nix profile` on `/nix/var/nix/profiles/default` no longer breaks `nix upgrade-nix` [cl/952](https://gerrit.lix.systems/c/lix/+/952)

  On non-NixOS, Nix is conventionally installed into a `nix-env` style profile at /nix/var/nix/profiles/default.
  Like any `nix-env` profile, using `nix profile` on it automatically migrates it to a `nix profile` style profile, which is incompatible with `nix-env`.
  `nix upgrade-nix` previously relied solely on `nix-env` to do the upgrade, but now will work fine with either kind of profile.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

## Packaging
- Lix turns more internal bugs into crashes [cl/797](https://gerrit.lix.systems/c/lix/+/797) [cl/626](https://gerrit.lix.systems/c/lix/+/626)

  Lix now enables build options such as trapping on signed overflow and enabling
  libstdc++ assertions by default. These may find new bugs in Lix, which will
  present themselves as Lix processes aborting, potentially without an error
  message.

  If Lix processes abort on your machine, this is a bug. Please file a bug,
  ideally with the core dump (or information from it).

  On Linux, run `coredumpctl list`, find the crashed process's PID at
  the bottom of the list, then run `coredumpctl info THE-PID`. You can then paste
  the output into a bug report.

  On macOS, open the Console app from Applications/Utilities, select Crash
  Reports, select the crash report in question. Right click on it, select Open In
  Finder, then include that file in your bug report. [See the Apple
  documentation][apple-crashreport] for more details.

  [apple-crashreport]: https://developer.apple.com/documentation/xcode/acquiring-crash-reports-and-diagnostic-logs#Locate-crash-reports-and-memory-logs-on-the-device

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Stop vendoring toml11 [cl/675](https://gerrit.lix.systems/c/lix/+/675)

  We don't apply any patches to it, and vendoring it locks users into
  bugs (it hasn't been updated since its introduction in late 2021).

  Many thanks to [winter](https://git.lix.systems/winter) for this.
- Lix is built with meson [cl/580](https://gerrit.lix.systems/c/lix/+/580) [cl/627](https://gerrit.lix.systems/c/lix/+/627) [cl/628](https://gerrit.lix.systems/c/lix/+/628) [cl/707](https://gerrit.lix.systems/c/lix/+/707) [cl/711](https://gerrit.lix.systems/c/lix/+/711) [cl/712](https://gerrit.lix.systems/c/lix/+/712) [cl/719](https://gerrit.lix.systems/c/lix/+/719)

  Lix is built exclusively with the meson build system thanks to a huge team-wide
  effort, and the legacy `make`/`autoconf` based build system has been removed
  altogether. This improves maintainability of Lix, enables things like saving
  20% of compile times with precompiled headers, and generally makes the build
  less able to produce obscure incremental compilation bugs.

  Non-Nix-based downstream packaging needs rewriting accordingly.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad), [eldritch horrors](https://git.lix.systems/pennae), [jade](https://git.lix.systems/jade), [wiggles](https://git.lix.systems/rbt), and [winter](https://git.lix.systems/winter) for this.
- Upstart scripts removed [cl/574](https://gerrit.lix.systems/c/lix/+/574)

  Upstart scripts have been removed from Lix, since Upstart is obsolete and has
  not been shipped by any major distributions for many years. If these are
  necessary to your use case, please back port them to your packaging.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

## Development
- Clang build timing analysis [cl/587](https://gerrit.lix.systems/c/lix/+/587)

  We now have Clang build profiling available, which generates Chrome
  tracing files for each compilation unit. To enable it, run `meson configure
  build -Dprofile-build=enabled` in a Clang stdenv (`nix develop
  .#native-clangStdenvPackages`) then rerun the compilation.

  If you want to make the build go faster, do a clang build with meson, then run
  `maintainers/buildtime_report.sh build`, then contemplate how to improve the
  build time.

  You can also look at individual object files' traces in
  <https://ui.perfetto.dev>.

  See [the wiki page][improving-build-times-wiki] for more details on how to do
  this.

  [improving-build-times-wiki]: https://wiki.lix.systems/link/8#bkmrk-page-title

## Miscellany
- Disallow empty search regex in `nix search` [#9481](https://github.com/NixOS/nix/pull/9481)

  [`nix search`](@docroot@/command-ref/new-cli/nix3-search.md) now requires a search regex to be passed. To show all packages, use `^`.

  Many thanks to [iFreilicht](https://github.com/iFreilicht) and [eldritch horrors](https://git.lix.systems/pennae) for this.
- `nix repl` history is saved more reliably [cl/1164](https://gerrit.lix.systems/c/lix/+/1164)

  `nix repl` now saves its history file after each line, rather than at the end
  of the session; ensuring that it will remember what you typed even after it
  crashes.

  Many thanks to [puck](https://git.lix.systems/puck) for this.
