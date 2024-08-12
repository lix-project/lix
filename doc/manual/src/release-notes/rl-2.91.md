# Lix 2.91 "Dragon's Breath" (2024-08-12)


# Lix 2.91.0 (2024-08-12)

## Breaking Changes
- Block io_uring in the Linux sandbox [cl/1611](https://gerrit.lix.systems/c/lix/+/1611)

  The io\_uring API has the unfortunate property that it is not possible to selectively decide which operations should be allowed.
  This, together with the fact that new operations are routinely added, makes it a hazard to the proper function of the sandbox.

  Therefore, any access to io\_uring has been made unavailable inside the sandbox.
  As such, attempts to execute any system calls forming part of this API will fail with the error `ENOSYS`, as if io\_uring support had not been configured into the kernel.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.
- The `build-hook` setting is now deprecated

  Build hooks communicate with the daemon using a custom, internal, undocumented protocol that is entirely unversioned and cannot be changed.
  Since we intend to change it anyway we must unfortunately deprecate the current build hook infrastructure.
  We do not expect this to impact most users—we have not found any uses of `build-hook` in the wild—but if this does affect you, we'd like to hear from you!
- Lix no longer speaks the Nix remote-build worker protocol to clients or servers older than CppNix 2.3 [fj#325](https://git.lix.systems/lix-project/lix/issues/325) [cl/1207](https://gerrit.lix.systems/c/lix/+/1207) [cl/1208](https://gerrit.lix.systems/c/lix/+/1208) [cl/1206](https://gerrit.lix.systems/c/lix/+/1206) [cl/1205](https://gerrit.lix.systems/c/lix/+/1205) [cl/1204](https://gerrit.lix.systems/c/lix/+/1204) [cl/1203](https://gerrit.lix.systems/c/lix/+/1203) [cl/1479](https://gerrit.lix.systems/c/lix/+/1479)

  CppNix 2.3 was released in 2019, and is the new oldest supported version. We
  will increase our support baseline in the future up to a final version of CppNix
  2.18 (which may happen soon given that it is the only still-packaged and thus
  still-tested >2.3 version), but this step already removes a significant amount
  of dead, untested, code paths.

  Lix speaks the same version of the protocol as CppNix 2.18 and that fact will
  never change in the future; the Lix plans to replace the protocol for evolution
  will entail a complete incompatible replacement that will be supported in
  parallel with the old protocol. Lix will thus retain remote build compatibility
  with CppNix as long as CppNix maintains protocol compatibility with 2.18, and
  as long as Lix retains legacy protocol support (which will likely be a long
  time given that we plan to convert it to a frozen-in-time shim).

  Many thanks to [jade](https://git.lix.systems/jade) for this.

## Features
- Pipe operator `|>` (experimental) [fj#438](https://git.lix.systems/lix-project/lix/issues/438) [cl/1654](https://gerrit.lix.systems/c/lix/+/1654)

  Implementation of the pipe operator (`|>`) in the language as described in [RFC 148](https://github.com/NixOS/rfcs/pull/148).
  The feature is still marked experimental, enable `--extra-experimental-features pipe-operator` to use it.

  Many thanks to [piegames](https://git.lix.systems/piegames) and [eldritch horrors](https://git.lix.systems/pennae) for this.

## Improvements
- Trace which part of a `foo.bar.baz` expression errors [cl/1505](https://gerrit.lix.systems/c/lix/+/1505) [cl/1506](https://gerrit.lix.systems/c/lix/+/1506)

  Previously, if an attribute path selection expression like `linux_4_9.meta.description` it wouldn't show you which one of those parts in the attribute path, or even that that line of code is what caused evaluation of the failing expression.
  The previous error looks like this:

  ```
  pkgs.linuxKernel.kernels.linux_4_9.meta.description

  error:
         … while evaluating the attribute 'linuxKernel.kernels.linux_4_9.meta.description'
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |     ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         … while calling the 'throw' builtin
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |                 ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         error: linux 4.9 was removed because it will reach its end of life within 22.11
  ```

  Now, the error will look like this:

  ```
  pkgs.linuxKernel.kernels.linux_4_9.meta.description

  error:
         … while evaluating the attribute 'linuxKernel.kernels.linux_4_9.meta.description'
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |     ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         … while evaluating 'pkgs.linuxKernel.kernels.linux_4_9' to select 'meta' on it
           at «string»:1:1:
              1| pkgs.linuxKernel.kernels.linux_4_9.meta.description
               | ^

         … caused by explicit throw
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |                 ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         error: linux 4.9 was removed because it will reach its end of life within 22.11
  ```

  Not only does the line of code that referenced the failing attribute show up in the trace, it also tells you that it was specifically the `linux_4_9` part that failed.

  This includes if the failing part is a top-level binding:

  ```
  let
    inherit (pkgs.linuxKernel.kernels) linux_4_9;
  in linux_4_9.meta.description
  error:
         … while evaluating 'linux_4_9' to select 'meta.description' on it
           at «string»:3:4:
              2|   inherit (pkgs.linuxKernel.kernels) linux_4_9;
              3| in linux_4_9.meta.description
               |    ^

         … while evaluating the attribute 'linux_4_9'
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:5:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |     ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         … caused by explicit throw
           at /nix/store/dk2rpyb6ndvfbf19bkb2plcz5y3k8i5v-source/pkgs/top-level/linux-kernels.nix:278:17:
            277|   } // lib.optionalAttrs config.allowAliases {
            278|     linux_4_9 = throw "linux 4.9 was removed because it will reach its end of life within 22.11";
               |                 ^
            279|     linux_4_14 = throw "linux 4.14 was removed because it will reach its end of life within 23.11";

         error: linux 4.9 was removed because it will reach its end of life within 22.11
  ```

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- Confusing 'invalid path' errors are now 'path does not exist' [cl/1161](https://gerrit.lix.systems/c/lix/+/1161) [cl/1160](https://gerrit.lix.systems/c/lix/+/1160) [cl/1159](https://gerrit.lix.systems/c/lix/+/1159)

  Previously, if a path did not exist in a Nix store, it was referred to as the internal name "path is invalid".
  This is, however, very confusing, and there were numerous such errors that were exactly the same, making it hard to debug.
  These errors are now more specific and refer to the path not existing in the store.

  Many thanks to [julia](https://git.lix.systems/midnightveil) for this.
- Add a `build-dir` setting to set the backing directory for builds [gh#10303](https://github.com/NixOS/nix/pull/10303) [gh#10312](https://github.com/NixOS/nix/pull/10312) [gh#10883](https://github.com/NixOS/nix/pull/10883) [cl/1514](https://gerrit.lix.systems/c/lix/+/1514)

  `build-dir` can now be set in the Nix configuration to choose the backing directory for the build sandbox.
  This can be useful on systems with `/tmp` on tmpfs, or simply to relocate large builds to another disk.

  Also, `XDG_RUNTIME_DIR` is no longer considered when selecting the default temporary directory,
  as it's not intended to be used for large amounts of data.

  Many thanks to [Robert Hensing](https://github.com/roberth) and [Tom Bereknyei](https://github.com/tomberek) for this.
- Better usage of colour control environment variables [cl/1699](https://gerrit.lix.systems/c/lix/+/1699) [cl/1702](https://gerrit.lix.systems/c/lix/+/1702)

  Lix now heeds `NO_COLOR`/`NOCOLOR` for more output types, such as that used in `nix search`, `nix flake metadata` and similar.

  It also now supports `CLICOLOR_FORCE`/`FORCE_COLOR` to force colours regardless of whether there is a terminal on the other side.

  It now follows rules compatible with those described on <https://bixense.com/clicolors/> with `CLICOLOR` defaulted to enabled.

  That is to say, the following procedure is followed in order:
  - NO_COLOR or NOCOLOR set

    Always disable colour
  - CLICOLOR_FORCE or FORCE_COLOR set

    Enable colour
  - The output is a tty; TERM != "dumb"

    Enable colour
  - Otherwise

    Disable colour

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Distinguish between explicit throws and errors that happened while evaluating a throw [cl/1511](https://gerrit.lix.systems/c/lix/+/1511)

  Previously, errors caused by an expression like `throw "invalid argument"` were treated like an error that happened simply while some builtin function was being called:

  ```
  let
    throwMsg = p: throw "${p} isn't the right package";
  in throwMsg "linuz"

  error:
         … while calling the 'throw' builtin
           at «string»:2:17:
              1| let
              2|   throwMsg = p: throw "${p} isn't the right package";
               |                 ^
              3| in throwMsg "linuz"

         error: linuz isn't the right package
  ```

  But the error didn't just happen "while" calling the `throw` builtin — it's a throw error!
  Now it looks like this:

  ```
  let
    throwMsg = p: throw "${p} isn't the right package";
  in throwMsg "linuz"

  error:
         … caused by explicit throw
           at «string»:2:17:
              1| let
              2|   throwMsg = p: throw "${p} isn't the right package";
               |                 ^
              3| in throwMsg "linuz"

         error: linuz isn't the right package
  ```

  This also means that incorrect usage of `throw` or errors evaluating its arguments are easily distinguishable from explicit throws:

  ```
  let
    throwMsg = p: throw "${p} isn't the right package";
  in throwMsg { attrs = "error when coerced in string interpolation"; }

  error:
         … while calling the 'throw' builtin
           at «string»:2:17:
              1| let
              2|   throwMsg = p: throw "${p} isn't the right package";
               |                 ^
              3| in throwMsg { attrs = "error when coerced in string interpolation"; }

         … while evaluating a path segment
           at «string»:2:24:
              1| let
              2|   throwMsg = p: throw "${p} isn't the right package";
               |                        ^
              3| in throwMsg { attrs = "error when coerced in string interpolation"; }

         error: cannot coerce a set to a string: { attrs = "error when coerced in string interpolation"; }
  ```

  Here, instead of an actual thrown error, a type error happens first (trying to coerce an attribute set to a string), but that type error happened *while* calling `throw`.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- `nix flake metadata` prints modified date [cl/1700](https://gerrit.lix.systems/c/lix/+/1700)

  Ever wonder "gee, when *did* I update nixpkgs"?
  Wonder no more, because `nix flake metadata` now simply tells you the times every locked flake input was updated:

  ```
  <...>
  Description:   The purely functional package manager
  Path:          /nix/store/c91yi8sxakc2ry7y4ac1smzwka4l5p78-source
  Revision:      c52cff582043838bbe29768e7da232483d52b61d-dirty
  Last modified: 2024-07-31 22:15:54
  Inputs:
  ├───flake-compat: github:edolstra/flake-compat/0f9255e01c2351cc7d116c072cb317785dd33b33
  │   Last modified: 2023-10-04 06:37:54
  ├───nix2container: github:nlewo/nix2container/3853e5caf9ad24103b13aa6e0e8bcebb47649fe4
  │   Last modified: 2024-07-10 13:15:56
  ├───nixpkgs: github:NixOS/nixpkgs/e21630230c77140bc6478a21cd71e8bb73706fce
  │   Last modified: 2024-07-25 11:26:27
  ├───nixpkgs-regression: github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2
  │   Last modified: 2022-01-24 11:20:45
  └───pre-commit-hooks: github:cachix/git-hooks.nix/f451c19376071a90d8c58ab1a953c6e9840527fd
      Last modified: 2024-07-15 04:21:09
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Hash mismatch diagnostics for fixed-output derivations include the URL [cl/1536](https://gerrit.lix.systems/c/lix/+/1536)

  Now, when building fixed-output derivations, Lix will guess the URL that was used in the derivation using the `url` or `urls` properties in the derivation environment.
  This is a layering violation but making these diagnostics tractable when there are multiple instances of the `AAAA` hash is too significant of an improvement to pass it up.

  ```
  error: hash mismatch in fixed-output derivation '/nix/store/sjfw324j4533lwnpmr5z4icpb85r63ai-x1.drv':
          likely URL: https://meow.puppy.forge/puppy.tar.gz
           specified: sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
              got:    sha256-a1Qvp3FOOkWpL9kFHgugU1ok5UtRPSu+NwCZKbbaEro=
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Add log formats `multiline` and `multiline-with-logs` [cl/1369](https://gerrit.lix.systems/c/lix/+/1369)

  Added two new log formats (`multiline` and `multiline-with-logs`) that display
  current activities below each other for better visibility.

  These formats attempt to use the maximum available lines
  (defaulting to 25 if unable to determine) and print up to that many lines.
  The status bar is displayed as the first line, with each subsequent
  activity on its own line.

  Many thanks to [kloenk](https://git.lix.systems/kloenk) for this.
- Lix will now show the package descriptions in when running `nix flake show`. [cl/1540](https://gerrit.lix.systems/c/lix/+/1540)

  When running `nix flake show`, Lix will now show the package descriptions, if they exist.

  Before:

  ```shell
  $ nix flake show
  path:/home/isabel/dev/lix-show?lastModified=1721736108&narHash=sha256-Zo8HP1ur7Q2b39hKUEG8EAh/opgq8xJ2jvwQ/htwO4Q%3D
  └───packages
      └───x86_64-linux
          ├───aNoDescription: package 'simple'
          ├───bOneLineDescription: package 'simple'
          ├───cMultiLineDescription: package 'simple'
          ├───dLongDescription: package 'simple'
          └───eEmptyDescription: package 'simple'
  ```

  After:

  ```shell
  $ nix flake show
  path:/home/isabel/dev/lix-show?lastModified=1721736108&narHash=sha256-Zo8HP1ur7Q2b39hKUEG8EAh/opgq8xJ2jvwQ/htwO4Q%3D
  └───packages
      └───x86_64-linux
          ├───aNoDescription: package 'simple'
          ├───bOneLineDescription: package 'simple' - 'one line'
          ├───cMultiLineDescription: package 'simple' - 'line one'
          ├───dLongDescription: package 'simple' - 'abcdefghijklmnopqrstuvwxyz'
          └───eEmptyDescription: package 'simple'
  ```

  Many thanks to [kjeremy](https://github.com/kjeremy) and [isabelroses](https://git.lix.systems/isabelroses) for this.
- Eliminate some pretty-printing surprises [#11100](https://github.com/NixOS/nix/pull/11100) [cl/1616](https://gerrit.lix.systems/c/lix/+/1616) [cl/1617](https://gerrit.lix.systems/c/lix/+/1617) [cl/1618](https://gerrit.lix.systems/c/lix/+/1618)

  Some inconsistent and surprising behaviours have been eliminated from the pretty-printing used by the REPL and `nix eval`:
  * Lists and attribute sets that contain only a single item without nested structures are no longer sometimes inappropriately indented in the REPL, depending on internal state of the evaluator.
  * Empty attribute sets and derivations are no longer shown as `«repeated»`, since they are always cheap to print.
    This matches the existing behaviour of `nix-instantiate` on empty attribute sets.
    Empty lists were never printed as `«repeated»` already.
  * The REPL by default does not print nested attribute sets and lists, and indicates elided items with an ellipsis.
    Previously, the ellipsis was printed even when the structure was empty, so that such items do not in fact exist.
    Since this behaviour was confusing, it does not happen any more.

  Before:
  ```
  nix-repl> :p let x = 1 + 2; in [ [ x ] [ x ] ]
  [
    [
      3
    ]
    [ 3 ]
  ]

  nix-repl> let inherit (import <nixpkgs> { }) hello; in [ hello hello ]
  [
    «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
    «repeated»
  ]

  nix-repl> let x = {}; in [ x ]
  [
    { ... }
  ]
  ```

  After:
  ```
  nix-repl> :p let x = 1 + 2; in [ [ x ] [ x ] ]
  [
    [ 3 ]
    [ 3 ]
  ]

  nix-repl> let inherit (import <nixpkgs> { }) hello; in [ hello hello ]
  [
    «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
    «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
  ]

  nix-repl> let x = {}; in [ x ]
  [
    { }
  ]
  ```

  Many thanks to [alois31](https://git.lix.systems/alois31) and [Robert Hensing](https://github.com/roberth) for this.
- `nix registry add` now requires a shorthand flakeref on the 'from' side [cl/1494](https://gerrit.lix.systems/c/lix/+/1494)

  The 'from' argument must now be a shorthand flakeref like `nixpkgs` or `nixpkgs/nixos-20.03`, making it harder to accidentally swap the 'from' and 'to' arguments.

  Registry entries that map from other flake URLs can still be specified in registry.json, the `nix.registry` option in NixOS, or the `--override-flake` option in the CLI, but they are not guaranteed to work correctly.

  Many thanks to [delan](https://git.lix.systems/delan) for this.
- Allow automatic rejection of configuration options from flakes [cl/1541](https://gerrit.lix.systems/c/lix/+/1541)

  Setting `accept-flake-config` to `false` now respects user choice by automatically rejecting configuration options set by flakes.
  The old behaviour of asking each time is still available (and default) by setting it to the special value `ask`.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.
- `nix repl` now allows tab-completing the special repl :colon commands [cl/1367](https://gerrit.lix.systems/c/lix/+/1367)

  The REPL (`nix repl`) supports pressing `<TAB>` to complete a partial expression, but now also supports completing the special :colon commands as well (`:b`, `:edit`, `:doc`, etc), if the line starts with a colon.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.
- `:edit`ing a file in Nix store no longer reloads the repl [fj#341](https://git.lix.systems/lix-project/lix/issues/341) [cl/1620](https://gerrit.lix.systems/c/lix/+/1620)

  Calling `:edit` from the repl now only reloads if the file being edited was outside of Nix store.
  That means that all the local variables are now preserved across `:edit`s of store paths.
  This is always safe because the store is read-only.

  Many thanks to [goldstein](https://git.lix.systems/goldstein) for this.
- `:log` in repl now works on derivation paths [fj#51](https://git.lix.systems/lix-project/lix/issues/51) [cl/1716](https://gerrit.lix.systems/c/lix/+/1716)

  `:log` can now accept store derivation paths in addition to derivation expressions.

  Many thanks to [goldstein](https://git.lix.systems/goldstein) for this.

## Fixes
- Define integer overflow in the Nix language as an error [fj#423](https://git.lix.systems/lix-project/lix/issues/423) [cl/1594](https://gerrit.lix.systems/c/lix/+/1594) [cl/1595](https://gerrit.lix.systems/c/lix/+/1595) [cl/1597](https://gerrit.lix.systems/c/lix/+/1597) [cl/1609](https://gerrit.lix.systems/c/lix/+/1609)

  Previously, integer overflow in the Nix language invoked C++ level signed overflow, which was undefined behaviour, but *probably* manifested as wrapping around on overflow.

  Since prior to the public release of Lix, Lix had C++ signed overflow defined to crash the process and nobody noticed this having accidentally removed overflow from the Nix language for three months until it was caught by fiddling around.
  Given the significant body of actual Nix code that has been evaluated by Lix in that time, it does not appear that nixpkgs or much of importance depends on integer overflow, so it is safe to turn into an error.

  Some other overflows were fixed:
  - `builtins.fromJSON` of values greater than the maximum representable value in a signed 64-bit integer will generate an error.
  - `nixConfig` in flakes will no longer accept negative values for configuration options.

  Integer overflow now looks like the following:

  ```
  » nix eval --expr '9223372036854775807 + 1'
  error: integer overflow in adding 9223372036854775807 + 1
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Fix nix-collect-garbage --dry-run [fj#432](https://git.lix.systems/lix-project/lix/issues/432) [cl/1566](https://gerrit.lix.systems/c/lix/+/1566)

  `nix-collect-garbage --dry-run` did not previously give any output - it simply
  exited without even checking to see what paths would be deleted.

  ```
  $ nix-collect-garbage --dry-run
  $
  ```

  We updated the behaviour of the flag such that instead it prints out how many
  paths it *would* delete, but doesn't actually delete them.

  ```
  $ nix-collect-garbage --dry-run
  finding garbage collector roots...
  determining live/dead paths...
  ...
  <nix store paths>
  ...
  2670 store paths deleted, 0.00MiB freed
  $
  ```

  Many thanks to [Quantum Jump](https://github.com/QuantumBJump) for this.
- Fix unexpectedly-successful GC failures on macOS [fj#446](https://git.lix.systems/lix-project/lix/issues/446) [cl/1723](https://gerrit.lix.systems/c/lix/+/1723)

  Has the following happened to you on macOS? This failure has been successfully eliminated, thanks to our successful deployment of advanced successful-failure detection technology (it's just `if (failed && errno == 0)`. Patent pending<sup>not really</sup>):

  ```
  $ nix-store --gc --print-dead
  finding garbage collector roots...
  error: Listing pid 87261 file descriptors: Undefined error: 0
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- `nix copy` is now several times faster at `querying info about /nix/store/...` [fj#366](https://git.lix.systems/lix-project/lix/issues/366) [cl/1462](https://gerrit.lix.systems/c/lix/+/1462)

  We fixed a locking bug that serialized `querying info about /nix/store/...`
  onto just one thread such that it was eating `O(paths to copy * latency)` time
  while setting up to copy paths to s3 and other stores. It is now `nproc` times
  faster.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

## Development
- clang-tidy support [fj#147](https://git.lix.systems/lix-project/lix/issues/147) [cl/1697](https://gerrit.lix.systems/c/lix/+/1697)

  `clang-tidy` can be used to lint Lix with a limited set of lints using `ninja -C build clang-tidy` and `ninja -C build clang-tidy-fix`.
  In practice, this fixes the built-in meson rule that was used the same as above being broken ever since precompiled headers were introduced.

  Many thanks to [jade](https://git.lix.systems/jade) for this.
- Lix now supports building with UndefinedBehaviorSanitizer [cl/1483](https://gerrit.lix.systems/c/lix/+/1483) [cl/1481](https://gerrit.lix.systems/c/lix/+/1481) [cl/1669](https://gerrit.lix.systems/c/lix/+/1669)

  You can now build Lix with the configuration option `-Db_sanitize=undefined,address` and it will both work and pass tests with both AddressSanitizer and UndefinedBehaviorSanitizer enabled.
  To use ASan specifically, you have to set `-Dgc=disabled`, which an error message will tell you to do if necessary anyhow.

  Furthermore, tests passing with Clang ASan+UBSan is checked on every change in CI.

  For a list of undefined behaviour found by tooling usage, see [the gerrit topic "undefined-behaviour"](https://gerrit.lix.systems/q/topic:%22undefined-behaviour%22).

  Many thanks to [jade](https://git.lix.systems/jade) for this.
