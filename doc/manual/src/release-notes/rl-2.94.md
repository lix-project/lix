# Lix 2.94 "Açaí na tigela" (2025-11-17)


# Lix 2.94.0 (2025-11-17)
## Breaking Changes

- Remove support for daemon protocols before 2.18 [fj#510](https://git.lix.systems/lix-project/lix/issues/510) [cl/3249](https://gerrit.lix.systems/c/lix/+/3249)

  Support for daemon wire protocols belonging to Nix 2.17 or older have been
  removed. This impacts clients connecting to the local daemon socket or any
  remote builder configured using the `ssh-ng` protocol. Builders configured
  with the `ssh` protocol are still accessible from clients such as Nix 2.3.
  Additionally Lix will not be able to connect to an old daemon locally, and
  remote build connections to old daemons is likewise limited to `ssh` urls.

  We have decided to take this step because the old protocols are very badly
  tested (if at all), maintenance overhead is high, and a number of problems
  with their design makes it infeasible to remain backwards compatible while
  we move Lix to a more modern RPC mechanism with better versioning support.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Remove impure derivations and dynamic derivations [fj#815](https://git.lix.systems/lix-project/lix/issues/815) [cl/3210](https://gerrit.lix.systems/c/lix/+/3210)

  The `impure-derivations` and `dynamic-derivations` experimental feature have
  been removed.

  New impure or dynamic derivations cannot be created from this point forward, and
  any such pre-existing store derivations canot be read or built any more.
  Derivation outputs created by building such a derivation are still valid
  until garbage collected; existing store derivations can only be garbage
  collected.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- First argument to `--arg`/`--argstr` must be a valid Nix identifier [fj#496](https://git.lix.systems/lix-project/lix/issues/496)

  The first argument to `--arg`/`--argstr` must be a valid Nix identifier, i.e.
  `nix-build --arg config.allowUnfree true` is now rejected.

  This is because that invocation is a false friend since it doesn't set
  `{ config = { allowUnfree = true; }; }`, but `{ "config.allowUnfree" = true; }`.

  The idea is to change the behavior to the latter in the long-term. For that,
  non-identifiers started giving a warning since 2.92 and are now rejected to give people
  who depend on that a chance to notice and potentially weigh in on the discussion.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- New cgroup delegation model [fj#537](https://git.lix.systems/lix-project/lix/issues/537) [fj#77](https://git.lix.systems/lix-project/lix/issues/77) [cl/3230](https://gerrit.lix.systems/c/lix/+/3230)

  Builds using cgroups (i.e. `use-cgroups = true` and the experimental feature
  `cgroups`) now always delegate a cgroup tree to the sandbox.

  Compared to the original C++ Nix project, our delegation includes the
  `subtree_control` file as well, which means that the sandbox can disable
  certain controllers in its own cgroup tree.

  This is a breaking change because this requires the Nix daemon to run with an
  already delegated cgroup tree by the service manager.

  ## How to setup the cgroup tree with systemd?

  systemd offers knobs to perform the required setup using:

  ```
  [Service]
  Delegate=yes
  DelegateSubtree=supervisor
  ```

  These directives are now included in our systemd packaging.

  ## What about using Nix as root without connecting to the daemon?

  Builds run as `root` without connecting to the daemon relying on the cgroup
  feature are now broken, i.e.

  ```console
  # nix-build --use-cgroups --sandbox ... # will not work
  ```

  Consider doing instead:

  ```console
  # systemd-run --same-dir --wait -p Delegate=yes -p DelegateSubgroup=supervisor nix-build --use-cgroups ...
  ```

  If you need to disable cgroups temporarily, remember that you can do
  `NIX_CONF='include /etc/nix/nix.conf\nuse-cgroups = false' nix-build ...` or
  `nix-build --no-use-cgroups ...`.

  ## What about other service managers than systemd?

  systemd has a [documentation](https://systemd.io/CGROUP_DELEGATION/) on how to
  handle cgroup delegation from service management perspective.

  If your service manager adheres to systemd semantics, e.g. writing an extended
  attribute `user.delegate=1` on the delegated cgroup tree directory and moving
  the `nix-daemon` process inside a cgroup tree to respect the inner process
  rule, then, the feature will work as well.

  ## Why is the cgroup feature still experimental?

  While the cgroup feature unlocks many use cases, its behavior and integration (e.g. user experience), especially at scale on build farms or in multi-tenant environments, are not yet fully matured. There’s also potential for deeper systemd integration (e.g. using slices and scopes) that has not been fully explored.

  To avoid locking in an unstable interface, we’re keeping the experimental flag until we have validated the feature across a broader range of scenarios, including but not limited to:

  * Nix as root
  * Hydra-style build farms
  * Forgejo CI runners
  * Shared remote builders

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito), [eldritch horrors](https://git.lix.systems/pennae), and [lheckemann](https://git.lix.systems/lheckemann) for this.

- Enable high compress ratio zstd compression by default for binary caches uploads [fj#945](https://git.lix.systems/lix-project/lix/issues/945) [cl/4503](https://gerrit.lix.systems/c/lix/+/4503)

  The default compression method for binary cache uploads has been switched from
  [`xz`](https://github.com/tukaani-project/xz) to
  [`zstd`](https://github.com/facebook/zstd) to address performance and usability
  issues related to modern hardware and high-speed connections.

  ## Why?

  `xz` offers compression ratios but is single-threaded in our implementation and
  very slow (~10-20 Mbps in our test), preventing full utilization of 100Mbps+
  connections and significantly slowing decompression for end users.

  Lix is a "compress once, decompress many" application: build farms can afford
  to spend more time compressing to achieve a faster download transfer for the
  end user. More importantly, it matters that all end users spend the least
  amount of time decompressing.

  ## What about compression ratios?

  `zstd` cannot achieve the same peaks as `xz`, nonetheless, `zstd` compression
  level has been increased to level 12 by default to balance compression ratio
  and performance.

  ## Synthetic test case data

  * **xz** (default compression level) on a 4.4GB file: ~632MB (77s)
  * **zstd** (level 12) on the same file: ~775MB (18s), 18% larger but 50% faster
  * **zstd** (level 14): ~773MB (37s)
  * **zstd** (level 16): ~735MB (66s)

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) and [Raito Bezarius](https://git.lix.systems/raito) for this.

- Repl debugger uses `--ignore-try` by default [lix#666](https://git.lix.systems/lix-project/lix/issues/666) [cl/3488](https://gerrit.lix.systems/c/lix/+/3488)

  Previously, using the debugger meant that exceptions thrown in `builtins.tryEval` would trigger the debugger.

  However, this caught nixpkgs initialization code, which is unhelpful in the majority of cases, so we changed the default.

  To get the old behaviour, use `--no-ignore-try`.

  ```
  $ nix repl --debugger --expr 'with import <nixpkgs> {}; pkgs.hello'
  Lix 2.94.0-dev-pre20250625-9a59106
  Type :? for help.
  error: file 'nixpkgs-overlays' was not found in the Nix search path (add it using $NIX_PATH or -I)

  This exception occurred in a 'tryEval' call. Use --ignore-try to skip these.

  Added 13 variables.
  nix-repl>
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Strings may now contain NUL bytes [cl/3968](https://gerrit.lix.systems/c/lix/+/3968)

  Lix now allows strings to contain NUL bytes instead of silently truncating the
  string before the first such byte. Notably NUL-bearing strings were allowed as
  attribute names—even though the corresponding strings were not representable!—
  leading to very surprising and incorrect behavior in corner cases, for example

  ```
  nix-repl> builtins.fromJSON ''{"a": 1, "a\u0000b": 2}''
  {
    a = 1;
    "ab" = 2;
  }

  nix-repl> builtins.attrNames (builtins.fromJSON ''{"a": 1, "a\u0000b": 2}'')
  [
    "a"
    "a"
  ]
  ```

  rather than the more correct but still with the terminal eating NUL on display

  ```
  nix-repl> builtins.fromJSON ''{"a": 1, "a\u0000b": 2}''
  {
    a = 1;
    "ab" = 2;
  }

  nix-repl> builtins.attrNames (builtins.fromJSON ''{"a": 1, "a\u0000b": 2}'')
  [
    "a"
    "ab"
  ]
  ```

  We consider this a breaking change since eval results *will* change if strings
  with embedded NUL bytes were used, but we also consider the old behavior to be
  not intentional (seeing how inconsistent it was) but merely fallout from a old
  and misguided implementation decision to be worked around, not actually fixed.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Fixed output derivations can be run using `pasta` network isolation [fj#285](https://git.lix.systems/lix-project/lix/issues/285) [cl/3452](https://gerrit.lix.systems/c/lix/+/3452)

  Fixed output derivations traditionally run in the host network namespace.
  On Linux this allows such derivations to communicate with other sandboxes
  or the host using the abstract Unix domains socket namespace; this hasn't
  been unproblematic in the past and has been used in two distinct exploits
  to break out of the sandbox. For this reason fixed output derivations can
  now run in a network namespace (provided by [`pasta`]), restricted to TCP
  and UDP communication with the rest of the world. When enabled this could
  be a breaking change and we classify it as such, even though we don't yet
  enable or require such isolation by default. We may enforce this in later
  releases of Lix once we have sufficient confidence that breakage is rare.

  [`pasta`]: https://passt.top/

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) and [puck](https://git.lix.systems/puck) for this.

- Function equality semantics are more consistent, but still bad [cl/4556](https://gerrit.lix.systems/c/lix/+/4556) [cl/4244](https://gerrit.lix.systems/c/lix/+/4244)

  Lix has inherited a historic misfeature from CppNix in the form of pointer
  equality checks built into the `==` operator. These checks were originally
  meant to optimize comparison for large sets, but they have the unfortunate
  side effect of producing unexpected results when sets containing functions
  are compared. **Lix 2.93 and earlier** behave as shown in the repl session

  ```
  Lix 2.93.3
  Type :? for help.
  nix-repl> f = x: x
  Added f.

  nix-repl> f == f
  false

  nix-repl> let s.f = f; in s.f == s.f
  false

  nix-repl> # however!
            { inherit f; } == { inherit f; }
  true

  nix-repl> [ f ] == [ f ]
  true

  nix-repl> # and, in another twist:
            [ f ] == map f [ f ]
  false
  ```

  Nixpkgs relies on sets containing functions being comparable, so we cannot
  simply deprecate this behavior. Due to changes to the object model used by
  Lix ***all* comparisons above now evaluate to `true`**. This is considered
  a breaking change because eval results may differ, but we also consider it
  minor because the optimization is unsound (c.f. `let l = [NaN]; in l == l`
  evaluates to `true` even though floating point `NaN` is incomparable). Lix
  intends to remove this optimization altogether in the future, but until we
  can do that we instead make it slightly less broken to allow other, *real*
  optimizations. Function equality comparison remains **undefined behavior**
  and should not be relied upon in Nixlang code that intends to be portable.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- `nix eval --write-to` has been removed [fj#974](https://git.lix.systems/lix-project/lix/issues/974) [fj#227](https://git.lix.systems/lix-project/lix/issues/227) [cl/4045](https://gerrit.lix.systems/c/lix/+/4045)

  `nix eval --write-to` has been removed since it was underspecified, not widely
  useful, and prone to security-sensitive misbehaviors. The feature was added in
  Nix 2.4 purely for internal use in the build system. According to our research
  it hasn't found any use outside of some distribution packaging scripts. Please
  use structured outputs formats (such as JSON) instead as they have better type
  fidelity, don't conflate attributes with paths, and are useful to other tools.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Remove the `parse-toml-timestamps` experimental feature

  The `parse-toml-timestamps` experimental feature has been removed.

  This feature used in‐band signalling to mark timestamps, making it
  impossible to unambiguously parse TOML documents. It also exposed
  implementation‐defined behaviour in the TOML specification that
  changed in the toml11 parser library.

  Any interface for parsing TOML timestamps suitable for future
  stabilization would necessarily involve breaking changes, and there
  is no evidence this experimental feature is being relied upon in the
  wild, so it has been removed.

  Many thanks to [Emily](https://git.lix.systems/emilazy) for this.

- Reject overflowing TOML integer literals [cl/3916](https://gerrit.lix.systems/c/lix/+/3916)

  The toml11 library used by Lix was updated. The new
  version aligns with the [TOML v1.0.0 specification’s
  requirement](https://toml.io/en/v1.0.0#integer) to reject integer
  literals that cannot be losslessly parsed. This means that code like
  `builtins.fromTOML "v=0x8000000000000000"` will now produce an error
  rather than silently saturating the integer result.

  Many thanks to [Emily](https://git.lix.systems/emilazy) for this.

- uid-range depends on cgroups [cl/3230](https://gerrit.lix.systems/c/lix/+/3230)

  `uid-range` builds now depends on `cgroups`, an experimental feature.

  `uid-range` builds already depended upon `auto-allocate-uids`, another experimental feature.

  The rationale for doing so is that `uid-range` provides a sandbox with many
  UIDs, this is useful for re-mapping them into a nested namespace, e.g. a
  container.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [eldritch horrors](https://git.lix.systems/pennae) for this.


## Features

- Add `inputs.self.submodules` flake attribute [fj#942](https://git.lix.systems/lix-project/lix/issues/942) [cl/3839](https://gerrit.lix.systems/c/lix/+/3839)

  A port of <https://github.com/NixOS/nix/pull/12421> to Lix, which:

  - adds a general `inputs.self` flake attribute that retroactively applies
    configurations to a flake after it's been fetched, then triggers a refetch of
    the flake with the new config.
  - implements `inputs.self.submodules` that allows a flake to declare its need
    for submodules, which are then fetched automatically with no need to pass
    `?submodules=1` anywhere.

  Many thanks to [Eelco Dolstra](https://github.com/edolstra) and [ورد](https://git.lix.systems/janw4ld) for this.

- Lix supports HTTP/3 behind `--http3` [fj#1033](https://git.lix.systems/lix-project/lix/issues/1033)

  Lix now supports HTTP/3 for file transfers when the linked curl version
  supports it.

  By default, HTTP/3 is disabled notably due to performance issues reported in
  mid-2024. [More details
  here](https://daniel.haxx.se/blog/2024/06/10/http-3-in-curl-mid-2024/).

  As of 2025-11-14, [NixOS official cache](https://cache.nixos.org) supports
  HTTP/3 via Fastly. [More info
  here](https://github.com/NixOS/infra/commit/157fa70e46afbd6338a32407be461fce05c57bf8).

  To enable HTTP/3:

  * Use `--http3` for individual transfers.
  * Add `http3 = true` in your Nix configuration for permanent activation.

  To disable it, use `--no-http3`.

  **Note**:

  * `--no-http2 --http3` will still enable both HTTP/2 and HTTP/3.
  * `--http2 --http3` will prioritize HTTP/3 and fall back to HTTP/2 (and then
    HTTP/1.1).

  These are current CLI limitations. In the future, we plan to replace `--httpX`
  options with `--max-http-version [1,2,3]` for easier version selection in Lix
  transfers.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Add hyperlinks in attr set printing [cl/3790](https://gerrit.lix.systems/c/lix/+/3790)

  The attribute set printer, such as is seen in `nix repl` or in type errors, now prints hyperlinks on each attribute name to its definition site if it is known.

  Example: all of the attributes shown here are hyperlinks to the exact definition site of the attribute in question:

  ```
  $ nix eval -f '<nixpkgs>' lib.licenses.mit
  { deprecated = false; free = true; fullName = "MIT License"; redistributable = true; shortName = "mit"; spdxId = "MIT"; url = "https://spdx.org/licenses/MIT.html"; }
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Experimental integer coercion in interpolated strings [cl/3198](https://gerrit.lix.systems/c/lix/+/3198)

  Ever tried interpolating a port number in Lix and ended up with something like this?

  ```nix
  "http://${config.network.host}:${builtins.toString config.network.port}/"
  ```

  You're not alone. Thousands of Lix users suffer every day from excessive `builtins.toString` syndrome. It’s 2025, and we still have to cast integers to use them in strings.

  To address this, Lix introduces the **`coerce-integers`** experimental feature. When enabled, interpolated integers within `"${...}"` are automatically coerced to strings. This allows writing:

  ```nix
  "http://${config.network.host}:${config.network.port}/"
  ```

  without additional conversion.

  To enable the feature, you need to add `coerce-integers` to your set of experimental features.

  ### Stabilization criteria

  The `coerce-integers` feature is experimental and limited strictly to string interpolation (`"${...}"`). Before stabilization, the following must hold:

  1. **Interpolation-only**
     Coercion must not occur outside interpolation. Expressions like `"" + 42` must continue to fail.

  2. **Expectation that no explicit cast are being observed**
     Cases observing explicit coercion (e.g., via `tryEval` gadget or similar) are expected not to be load-bearing in actual production code.

  ### Timeline for stabilization

  If the feature proves safe and is widely adopted across typical usage (e.g., actual configurations in the wild turning on the flag, non-trivial out-of-tree projects using it), the experimental flag will be removed **after six months of active use or two Lix releases**, whichever is longer.

  This avoids locking the feature in experimental status indefinitely, as happened with Flakes, while allowing time for validation and ecosystem integration.

  ### What about coercing floats or more?

  Coercion beyond integers -- such as for floats or other types -- is **not planned**, even under an experimental flag. Questions like "what is the canonical string representation of a float?" involve subtle and context-dependent trade-offs. Without a robust and principled mechanism to define and audit such behavior, introducing broader coercion risks setting unintended and hard-to-reverse precedents. The scope of `coerce-integers` is intentionally narrow and will remain so.

  In terms of outlook, a proposal like https://git.lix.systems/lix-project/lix/issues/835 could pave the way for a better solution.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito), [delroth](https://github.com/delroth), [eldritch horrors](https://git.lix.systems/pennae), and [winter](https://git.lix.systems/winter) for this.

- nix-eval-jobs: support `--no-instantiate` flag [fj#987](https://git.lix.systems/lix-project/lix/issues/987)

  `nix-eval-jobs` now supports a flag called `--no-instantiate`. With this enabled,
  no write operations on the eval store are performed. That means, only evaluation is
  performed, but derivations (and their gcroots) aren't created.

  Many thanks to [mic92](https://github.com/mic92) and [ma27](https://git.lix.systems/ma27) for this.


## Improvements

- Assess current profile generations pointers in `nix doctor` [cl/3108](https://gerrit.lix.systems/c/lix/+/3108)

  Added a new check to `nix doctor` that verifies whether the current generation of
  a Nix profile can be resolved. This helps users diagnose issues with broken or
  misconfigured profile symlinks.

  This helps determining if you have broken symlinks or misconfigured packaging.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Improved susbtituter query speed

  The code used to query substituters for derivations has been rewritten slightly
  to take advantage of our asynchronous runtime. Such queries run for every build
  that could download from substituters and processes every derivation that isn't
  yet present on the local system. Previously Lix would use `http-connections` to
  limit query concurrency, even for modern caches that support HTTP/2 and have no
  limit on how many queries can be run concurrently on one single connection. Lix
  no longer does this, resulting in approximately 60% reduction in query time for
  medium-sized closures (e.g. NixOS system closures) during testing, although the
  exact number depends greatly on local network latency and generally improves as
  latency increases. Unlike previously setting `http-connections` to `1` or other
  low values no longer brings a massive penalty in query performance if the cache
  in use by the querying system supports HTTP/2 (as e.g. `cache.nixos.org` does).

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Hitting Control-C twice always terminates Lix [cl/3574](https://gerrit.lix.systems/c/lix/+/3574)

  Hitting Control-C or sending `SIGINT` to Lix now prints an informational message
  if it is still running after on second, the second Control-C/`SIGINT` terminates
  Lix immediately without waiting for any shutdown code to finish running. Lix did
  not treat the second such event differently from first in the past; this made it
  impossible to easily terminate running Lix processes that got stuck in e.g. very
  expensive Nixlang code that never interacted with the store. We now terminate as
  soon as the user hits Control-C again without waiting any more, to much the same
  effect as putting Lix into the background and killing it immediately afterwards.

  This means you can now more conveniently break out of stuck Nixlang evaluations:
  ```
  ❯ nix-instantiate --eval --expr 'let f = n: if n == 0 then 0 else f (n - 1) + f (n - 1); in f 32'
  ^CStill shutting down. Press ^C again to abort all operations immediately.
  ^C

  ❌130 ❯
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- `--keep-failed` chowns the build directory to the user that request the build

  Running a build with `--keep-failed` now chowns the temporary directory from the
  builder user and group to the user that request the build if the build came from
  a local user connected to the daemon. This makes inspecting failed derivations a
  lot easier. On Linux the build directory made visible to the user will not be in
  the same path as it was in the sandbox and continuing builds will usually break.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Better debuggability on fixed-output hash mismatches

  Fixed-output derivation hash mismatch error messages will now include the path that was
  produced unexpectedly, and this path will be registered as valid even if `--check`
  (`nix-store`, `nix-build`) or `--rebuild` (`nix build`) was passed. This makes comparing
  the expected path with the obtained path easier, and is useful for debugging when
  upstreams modify previously-published releases or when changes in fixed-output
  derivations' dependencies affect their output unexpectedly.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Add --raw flag to `nix-instantiate --eval` for unescaped output [gh#12119](https://github.com/NixOS/nix/pull/12119) [cl/2886](https://gerrit.lix.systems/c/lix/+/2886)

  The `nix-instantiate --eval` command now supports a `--raw` flag. When used,
  the result must be coercible to a string (as with `${...}`) and is printed
  verbatim, without quotes or escaping.

  Many thanks to [Martin Fischer](https://github.com/not-my-profile), [infinisil](https://github.com/infinisil), and [Raito Bezarius](https://git.lix.systems/raito) for this.

- Allow `nix store ls` to read nar listings from binary cache stores. [cl/3225](https://gerrit.lix.systems/c/lix/+/3225)

  The `nix store ls` command now supports reading `.ls` nar listings from binary cache stores.
  If a listing is detected for the store path being queried, the nar is no longer downloaded.
  These nar listings are available in binary cache stores where the `write-nar-listing` option is
  enabled, such as cache.nixos.org.

  Many thanks to [Victor Fuentes](https://git.lix.systems/vlinkz) for this.

- show tree with references that lead to an output cycle [fj#551](https://git.lix.systems/lix-project/lix/issues/551)

  When Lix determines a cyclic dependency between several outputs of a derivation,
  it now displays which files in which outputs lead to an output cycle:

  ```
  error: cycle detected in build of '/nix/store/gc5h2whz3rylpf34n99nswvqgkjkigmy-demo.drv' in the references of output 'bar' from output 'foo'.

         Shown below are the files inside the outputs leading to the cycle:
         /nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar
         └───lib/libfoo: …stuffbefore /nix/store/h680k7k53rjl9p15g6h7kpym33250w0y-demo-baz andafter.…
             → /nix/store/h680k7k53rjl9p15g6h7kpym33250w0y-demo-baz
             └───share/snenskek: …???? /nix/store/dm24c76p9y2mrvmwgpmi64rryw6x5qmm-demo-foo ....…
                 → /nix/store/dm24c76p9y2mrvmwgpmi64rryw6x5qmm-demo-foo
                 └───bin/alarm: …textexttext/nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar abcabcabc.…
                     → /nix/store/3lrgm74j85nzpnkz127rkwbx3fz5320q-demo-bar
  ```

  Please note that showing the files and its contents while displaying the cycles only works
  on Linux.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Lix now enables parallel marking in boehm-gc [fj#983](https://git.lix.systems/lix-project/lix/issues/983) [cl/3880](https://gerrit.lix.systems/c/lix/+/3880)

  This brings a fairly modest performance improvement (~38% for `nixpkgs search hello`) to evaluation, especially in scenarios that necessitate larger heap sizes.

  Many thanks to [Eelco Dolstra](https://github.com/edolstra) and [Seth Flynn](https://git.lix.systems/getchoo) for this.

- `disallowedRequisites` now reports chains of disallowed requisites [fj#334](https://git.lix.systems/lix-project/lix/issues/334) [fj#626](https://git.lix.systems/lix-project/lix/issues/626) [gh#10877](https://github.com/NixOS/nix/issues/10877)

  When a build fails because of [`disallowedRequisites`](@docroot@/language/advanced-attributes.md#adv-attr-disallowedRequisites), the error message now includes the chain of references that led to the failure. This makes it easier to see in which derivations the chain can be broken, to resolve the problem.

  Example:

  ```
  $ nix-build -A hello
  error: output '/nix/store/0b7k85gg5r28gb54px9nq7iv5986mns9-hello-2.12.2' is not allowed to refer to the following paths:
         /nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-glibc-2.40-66
         Shown below are chains that lead to the forbidden path(s).
         /nix/store/0b7k85gg5r28gb54px9nq7iv5986mns9-hello-2.12.2
         └───/nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-glibc-2.40-66
  ```

  Many thanks to [ma27](https://git.lix.systems/ma27) and [Robert Hensing](https://github.com/roberth) for this.

- Stack traces now summarize involved derivations at the bottom [cl/4493](https://gerrit.lix.systems/c/lix/+/4493)

  When evaluation errors and a stack trace is printed,

  For example, if I add Nheko to a NixOS `environment.systemPackages` without adding `olm-3.2.16` `nixpkgs.config.permittedInsecurePackages`, then without `--show-trace`, I previously got this:

  ```
  error:
         … while calling the 'head' builtin
           at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/attrsets.nix:1701:13:
           1700|           if length values == 1 || pred here (elemAt values 1) (head values) then
           1701|             head values
               |             ^
           1702|           else

         … while evaluating the attribute 'value'
           at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/modules.nix:1118:7:
           1117|     // {
           1118|       value = addErrorContext "while evaluating the option `${showOption loc}':" value;
               |       ^
           1119|       inherit (res.defsFinal') highestPrio;

         (stack trace truncated; use '--show-trace' to show the full trace)

         error: Package ‘olm-3.2.16’ in /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/pkgs/by-name/ol/olm/package.nix:37 is marked as insecure, refusing to evaluate.

         < -snip the whole explanation about olm's CVEs- >
  ```

  This doesn't tell me anything about where `olm-3.2.16` came from.
  With `--show-trace`, there's 1 155 lines to sift through, but does contain lines like "while evaluating derivation 'nheko-0.12.1'".

  With this change, those lines are summarized and collected at the bottom, regardless of `--show-trace`:

  ```
  error:
         … while calling the 'head' builtin
           at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/attrsets.nix:1701:13:
           1700|           if length values == 1 || pred here (elemAt values 1) (head values) then
           1701|             head values
               |             ^
           1702|           else

         … while evaluating the attribute 'value'
           at /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/lib/modules.nix:1118:7:
           1117|     // {
           1118|       value = addErrorContext "while evaluating the option `${showOption loc}':" value;
               |       ^
           1119|       inherit (res.defsFinal') highestPrio;

         (stack trace truncated; use '--show-trace' to show the full trace)

         error: Package ‘olm-3.2.16’ in /nix/store/9v6qa656sq3xc58vkxslqy646p0ajj61-source/pkgs/by-name/ol/olm/package.nix:37 is marked as insecure, refusing to evaluate.


         < -snip the whole explanation about olm's CVEs- >


         note: trace involved the following derivations:
         derivation 'etc'
         derivation 'dbus-1'
         derivation 'system-path'
         derivation 'nheko-0.12.1'
         derivation 'mtxclient-0.10.1'
  ```

  Now we finally know that olm was evaluated because of Nheko, without sifting through *thousands* of lines of error message.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

- Symbols reuses once-allocated Value to reduce garbage collected allocations [cl/3308](https://gerrit.lix.systems/c/lix/+/3308) [cl/3300](https://gerrit.lix.systems/c/lix/+/3300) [cl/3314](https://gerrit.lix.systems/c/lix/+/3314) [cl/3310](https://gerrit.lix.systems/c/lix/+/3310) [cl/3312](https://gerrit.lix.systems/c/lix/+/3312) [cl/3313](https://gerrit.lix.systems/c/lix/+/3313)

  In the Lix evaluator, **symbols** represent immutable strings, like those used
  for attribute names.

  In evaluator design, such strings are typically [**interned**](https://en.wikipedia.org/wiki/String_interning), stored uniquely
  to save memory, and Lix inherits this approach from the original C++ codebase.

  However, some builtins, like `builtins.attrNames`, must return a `Value` type
  that can represent any Nix value (strings, integers, lists, etc.).

  Before this change, these builtins would create lists of `Value` objects by
  allocating them through the garbage collector, copying the symbol’s string
  content each time.

  This allocation is unnecessary if the interned symbols themselves also hold a
  `Value` representation allocated outside the garbage collector, since these
  live for the full duration of evaluation.

  As a result, this reduces the number of allocations, leading to:

  * A significant drop in maximum [resident set memory](https://en.wikipedia.org/wiki/Resident_set_size) (RSS), with some large-scale
    tests showing up to 11% (about 500 MiB) savings in large colmena deployments.
  * A slight decrease in CPU usage during Nix evaluations.

  This change is inspired by https://github.com/NixOS/nix/pull/13258 but the approach is different.

  **Note** : [`xokdvium`](https://github.com/xokdvium) is the rightful author of https://gerrit.lix.systems/c/lix/+/3300 and the credit was missed on our end during the development process. We are deeply sorry for this mistake.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito), [eldritch horrors](https://git.lix.systems/pennae), [Tom Hubrecht](https://git.lix.systems/tom-hubrecht), [xokdvium](https://github.com/xokdvium), and [NaN-git](https://github.com/NaN-git) for this.


## Fixes

- `build-dir` no longer defaults to `temp-dir` [cl/3453](https://gerrit.lix.systems/c/lix/+/3453)

  The directory in which temporary build directories are created no longer defaults
  to the value of the `temp-dir` setting to avoid builders making their directories
  world-accessible. This behavior has been used to escape the build sandbox and can
  cause build impurities even when not used maliciously. We now default to `builds`
  in `NIX_STATE_DIR` (which is `/nix/var/nix/b` in the default configuration).

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Global certificate authorities are copied inside the builder's environment [gh#12698](https://github.com/NixOS/nix/issues/12698) [fj#885](https://git.lix.systems/lix-project/lix/issues/885) [cl/3765](https://gerrit.lix.systems/c/lix/+/3765)

  Previously, CA certificates were only installed at
  `/etc/ssl/certs/ca-certificates.crt` for sandboxed builds on Linux.

  This setup was insufficient in light of recent changes in `nixpkgs`, which now
  enforce HTTPS usage for `fetchurl`, even for fixed-output derivations, to
  mitigate confidentiality risks such as `netrc` or credentials leakage.
  `nixpkgs` still make use of a special package called `cacerts` which contains a
  copy of the CA certificates maintained by Nixpkgs and added as a reference for
  TLS-enabled fetchers.

  As a result, having a consistent and trusted certificate authority in all
  builder environments is becoming more essential.

  On `nix-darwin`, the `NIX_SSL_CERT_FILE` environment variable is always
  explicitly defined, but it is ignored by the sandbox setup.

  Simultaneously, Nix evaluates and propagates impure environment variables via
  `lib.proxyImpureEnvVars`, meaning that if `NIX_SSL_CERT_FILE` is set (which
  influences the default value for `ssl-cert-file`), it will be forwarded
  unchanged into the builder environment.

  However, on Linux, Nix also *copies* the CA file into the sandbox, creating a
  discrepancy between the value of `NIX_SSL_CERT_FILE` and the actual trusted
  certificate path used during the build.

  This divergence caused confusion and was partially addressed by attempts to
  whitelist the CA path in the Darwin sandbox (see cl/2906), but that approach
  involved a non-trivial path canonicalization step and is not as general as this one.

  To address this properly, we now emit a warning and override
  `NIX_SSL_CERT_FILE` inside the builder, explicitly pointing it to the CA file
  copied into the sandbox.

  This eliminates ambiguity between `NIX_SSL_CERT_FILE`
  and `ssl-cert-file`, ensuring consistent trust anchors across platforms.

  This warning might become a hard error as we figure out what to do regarding
  `lib.proxyImpureEnvVars` in nixpkgs.

  The behavior has been verified across sandboxed and unsandboxed builds on both
  Linux and Darwin.

  As a consequence of this change, approximately 500 KB of CA certificate data is
  now unconditionally copied into the build directory for fixed-output
  derivations.

  While this ensures consistent trust verification without having to restart the
  daemon after system upgrades, it may introduce a slight overhead in build
  performance. At present, no optimizations have been implemented to avoid this
  copy, but if this overhead proves noticeable in your workflows, please open an
  issue so we can evaluate and possibly implement different strategies to render
  trust anchors visible.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [Emily](https://git.lix.systems/emilazy) for this.

- libstore: exponential backoff for downloads [lix#932](https://git.lix.systems/lix-project/lix/issues/932) [cl/3856](https://gerrit.lix.systems/c/lix/+/3856)

  The connection timeout when downloading from e.g. a binary cache is exponentially
  increased per failure. The option `connect-timeout` is now an alias to `max-connect-timeout`
  which is the maximum value for a timeout. The start value is controlled
  by `initial-connect-timeout` which is `5` by default.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Fix develop shells for derivations with escape codes [fj#991](https://git.lix.systems/lix-project/lix/issues/991) [cl/4154](https://gerrit.lix.systems/c/lix/+/4154) [cl/4155](https://gerrit.lix.systems/c/lix/+/4155)

  ASCII control characters (including `\e`, used for ANSI escape codes) in derivation variables are now correctly escaped for `nix develop` and `nix print-dev-env`, instead of erroring.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

- nix-store --delete: always remove obsolete hardlinks [cl/3188](https://gerrit.lix.systems/c/lix/+/3188)

  Deleting specific paths using `nix-store --delete` or `nix store
  delete` previously did not delete hard links created by `nix-store
  --optimise` even if they became obsolete, unless _all_ of the given
  paths were deleted successfully. Now, hard links are always cleaned
  up, even if some of the given paths could not be deleted.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Report GC statistics correctly [cl/3188](https://gerrit.lix.systems/c/lix/+/3188)

  Deleting specific paths using `nix-store --delete` or `nix store delete` previously did
  not report statistics correctly when some of the paths could not be deleted, even if
  others were deleted:

  ```
  $ nix store delete /nix/store/9bwryidal9q3g91cjm6xschfn4ikd82q-hello-2.12.1 --delete-closure -v
  finding garbage collector roots...
  deleting '/nix/store/9bwryidal9q3g91cjm6xschfn4ikd82q-hello-2.12.1'
  0 store paths deleted, 0.00 MiB freed
  error: Cannot delete some of the given paths because they are still alive. Paths not deleted:
           k9bxzr1l92r5y6mihrkbpbr3fmc8qszx-libidn2-2.3.8
           mbx9ii53lzjlrsnlrfmzpwm33ynljwdn-libunistring-1.3
           rf8hcy6bldxdqc0g6q1dcka1vh47x69s-xgcc-14.2.1.20250322-libgcc
           vbrdc5wgzn0w1zdp10xd2favkjn5fk7y-glibc-2.40-66
         To find out why, use nix-store --query --roots and nix-store --query --referrers.
  ```

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Fallback to safe temp dir when build-dir is unwritable [fj#876](https://git.lix.systems/lix-project/lix/issues/876) [cl/3501](https://gerrit.lix.systems/c/lix/+/3501)

  Non-daemon builds started failing with a permission error after introducing the `build-dir` option:

  ```
  $ nix build --store ~/scratch nixpkgs#hello --rebuild
  error: creating directory '/nix/var/nix/builds/nix-build-hello-2.12.2.drv-0': Permission denied
  ```

  This happens because:

  1. These builds are not run via the daemon, which owns `/nix/var/nix/builds`.
  2. The user lacks permissions for that path.

  We considered making `build-dir` a store-level option and defaulting it to `<chroot-root>/nix/var/nix/builds` for chroot stores, but opted instead for a fallback: if the default fails, Nix now creates a safe build directory under `/tmp`.

  To avoid CVE-2025-52991, the fallback uses an extra path component between `/tmp` and the build dir.

  **Note**: this fallback clutters `/tmp` with build directories that are not cleaned up. To prevent this, explicitly set `build-dir` to a path managed by Lix, even for local workloads.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Parse overflowing JSON number literals as floating‐point [cl/3919](https://gerrit.lix.systems/c/lix/+/3919)

  Previously, `builtins.fromJSON "-9223372036854775809"` would
  return a floating‐point number, while `builtins.fromJSON
  "9223372036854775808"` would cause an evaluation error. This was
  introduced with the banning of integer overflow in Lix 2.91; previously
  the latter would result in C++ undefined behaviour. These cases are
  now treated consistently with JSON’s model of a single numeric type,
  and JSON number literals that do not fit in a Nix‐language integer
  will be parsed as floating‐point numbers.

  Many thanks to [Emily](https://git.lix.systems/emilazy) for this.

- Fix handling of OSC codes in terminal output [fj#160](https://git.lix.systems/lix-project/lix/issues/160) [cl/3143](https://gerrit.lix.systems/c/lix/+/3143)

  OSC codes in terminal output are now handled correctly, where OSC 8 (hyperlink) is preserved any
  time color codes are allowed and all other OSC codes are stripped out. This applies not only to
  output from build commands but also to rendered documentation in the REPL.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.

- Fix nix develop for derivations that rejects dependencies with structured attrs [fj#997](https://git.lix.systems/lix-project/lix/issues/997) [cl/4182](https://gerrit.lix.systems/c/lix/+/4182)

  For the sake of concision, we refer to `disallowedReferences` in what follows,
  but all output checks were equally fixed:
  `{dis,}allowed{References,Requisites}`.

  Derivations can define *output checks* to reject unwanted dependencies, such as
  interpreters like `bash` or compilers like `gcc`. This can be done in two ways:

  * **Legacy style**: `disallowedReferences = [ ... ]` in the environment.
  * **Structured attrs**: `outputChecks.<output>.disallowedReferences = [ ... ]`,
    typically used in `__json`.

  Only the structured form supports derivations with multiple outputs.

  `nix develop` internally rewrites derivations to create development shells. It
  relied on the legacy `disallowedReferences`, and failed to honor the structured
  variant. This led to broken shells in cases where `bashInteractive` was
  explicitly disallowed using structured output checks, e.g. `nix develop
  nixpkgs#systemd` after the "bash-less NixOS" changes.

  This fix teaches `nix develop` to respect structured output checks, restoring
  support for such derivations.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- nix-eval-jobs: retain NIX_PATH [cl/3859](https://gerrit.lix.systems/c/lix/+/3859)

  `nix-eval-jobs` doesn't clear the `NIX_PATH` from the environment anymore. This matches the behavior
  of [upstream version `2.30`](https://github.com/nix-community/nix-eval-jobs/releases/tag/v2.30.0).

  Many thanks to [ma27](https://git.lix.systems/ma27) and [mic92](https://github.com/mic92) for this.

- Remove reliance on Bash for remote stores via SSH [fj#830](https://git.lix.systems/lix-project/lix/issues/830) [fj#805](https://git.lix.systems/lix-project/lix/issues/805) [fj#304](https://git.lix.systems/lix-project/lix/issues/304) [cl/3159](https://gerrit.lix.systems/c/lix/+/3159)

  The pre-flight `echo started` handshake -- added years ago to catch race conditions -- has been removed.

  After removal of connection sharing in Lix 2.93, it required a Bash-compatible shell and a standard `echo`, so it failed on:

    * builders protected by `ForceCommand` wrappers (e.g. `nix-remote-build`),
    * BusyBox / initrd images with no Bash,
    * hosts using non-POSIX shells such as Nushell.

  The race the probe once addressed was tied to SSH connection-sharing -- since connection-sharing code has already been removed, the probe is now pointless.

  Real connection or protocol errors are now left to SSH/Nix to report directly.

  This is technically a breaking change if you had scripts that relied on the literal "started" which needs to be updated to rely on other signals, e.g., exit codes.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- repl-overlays now work in the debugger for flakes [fj#777](https://git.lix.systems/lix-project/lix/issues/777) [cl/3398](https://gerrit.lix.systems/c/lix/+/3398)

  Due to a bug, it was previously not possible to use the debugger on flakes with repl-overlays, or with pure evaluation in general:

  ```
  $ nix repl --pure-eval
  Lix 2.94.0-dev-pre20250617-87d99da
  Type :? for help.
  Loading 'repl-overlays'...
  error: access to absolute path '/Users/jade/.config/nix/repl.nix' is forbidden in pure eval mode (use '--impure' to override)
  ```

  This is now fixed.
  The contents of the repl-overlays file itself (i.e. most typically the top level lambda in it) will be evaluated in impure mode.
  It may be necessary to use `builtins.seq` to force the impure operations to happen first if one wants to do impure operations inside a repl-overlays file in pure evaluation mode.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- `nix-shell` default shell directory is not `/tmp` anymore for `$NIX_BUILD_TOP` [fj#940](https://git.lix.systems/lix-project/lix/issues/940)

  Previously, Lix `nix-shell`s could exit non-zero status when `stdenv`'s `dumpVars` phase failed to write to `$NIX_BUILD_TOP/env-vars`, despite `dumpVars` being intended as a debugging aid.

  This happens when `TMPDIR` is not set and defaults therefore to `/tmp`, resulting in a `/tmp/env-vars` global file that every `nix-shell` wants to write.

  We fix this issue by reusing a pre-created, unique, and writable location, as the build top directory, avoiding shell exiting from write failures silently.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- libstore/binary-cache-store: don't cache narinfo on nix copy, remove negative entry [cl/3789](https://gerrit.lix.systems/c/lix/+/3789)

  When using e.g. [Snix's nar-bridge](https://snix.dev/docs/components/overview/#nar-bridge) via
  an `http`-store, Lix would create cache entries with a wrong URL to the NAR when uploading
  a store-path.

  This caused hard build failures for Hydra.

  Lix doesn't create these entries on upload anymore. Instead, it only removes negative cache entries.
  The cache entry for a narinfo is now created the first time, Lix queries the cache
  for the previously uploaded store-path again.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Lix libraries can now be linked statically [fj#789](https://git.lix.systems/lix-project/lix/issues/789) [cl/3775](https://gerrit.lix.systems/c/lix/+/3775) [cl/3778](https://gerrit.lix.systems/c/lix/+/3778)

  Previously the pkg-config files distributed with Lix were only suitable for dynamic linkage, causing "undefined reference to…" linker errors when trying to link statically.
  Private dependency information has now been added to make static linkage work as expected without user intervention.
  In addition, relevant static libraries are now prelinked to avoid strange failures due to missing static initializers.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.

- add description to zsh completions [fj#910](https://git.lix.systems/lix-project/lix/issues/910) [cl/3632](https://gerrit.lix.systems/c/lix/+/3632)

  Emit descriptions when completing args in zsh completions. This uses the descriptions we already
  provided in NIX\_GET\_COMPLETIONS.

  Many thanks to [matthewbauer](https://github.com/matthewbauer) for this.


## Miscellany

- Deprecation of CA derivations, dynamic derivations, and impure derivations [fj#815](https://git.lix.systems/lix-project/lix/issues/815)

  Content-addressed derivations are now deprecated and slated for removal in Lix 2.94.
  We're doing this because the CA derivation system has been a known cause of problems
  and inconsistencies, is unmaintained, habitually makes improving the store code very
  difficult (or blocks such improvements outright), and is beset by a number of design
  flaws that in our opinion cannot be fixed without a full reimplementation from zero.
  Dynamic derivations and impure derivations are built on the CA derivation framework,
  and owing to this they too are deprecated and slated for removal in another release.
