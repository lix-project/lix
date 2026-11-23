# Lix 2.96 "Bingsu" (2026-10-13)


# Lix 2.96.0 (2026-10-13)
## Breaking Changes

- builtins.floor/builtins.ceil handle out-of-range inputs correctly [nix#12899](https://github.com/NixOS/nix/issues/12899) [nix#13013](https://github.com/NixOS/nix/pull/13013) [cl/3923](https://gerrit.lix.systems/c/lix/+/3923)

  Previously, `builtins.floor` and `builtins.ceil` always cast the input into a floating point value before running the operation and casting the floating point result back into an integer.
  No checks were made for precision loss in either coercing integer inputs or converting the output to an integer (and in fact in the latter case, invoked undefined behaviour).

  Now, Lix checks for precision loss on integer input (to avoid a silent eval semantics change if we were to simply pass it through as-is) and on integer output.
  If your code fails to evaluate after this change, use `--extra-deprecated-features floor-ceil-corrupt-integers`.

  Many thanks to [jade](https://git.lix.systems/jade), [NaN-git](https://github.com/NaN-git), and [rootile (Rutile)](https://git.lix.systems/rootile) for this.

- Many nix3 command no longer build their arguments [cl/6424](https://gerrit.lix.systems/c/lix/+/6424)

  These nix3 cli commands no longer build the installables given to them directly:
  - `nix store` subcommands `dump-path`, `make-content-addressed`, `copy-sigs`, `sign`, `delete`, `verify`
  - `nix copy`
  - `nix path-info`

  Instead they expect paths given on the command line to already exist, independently of how they were provided (whether as store paths, flake attribute paths, outputs of files, you name it).
  We've chosen to change this because the previous behavior could be quite confusing, for example when deleting a path that was not present in the local store (which would be downloaded or even built only to immediately delete it again).
  If you relied on this implicit building behavior you will need to **update your workflows** to explicitly build all necessary path before passing them on.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Shadowing internal files through the Nix search path is now an error [lix#998](https://git.lix.systems/lix-project/lix/issues/998) [cl/4632](https://gerrit.lix.systems/c/lix/+/4632) [cl/5370](https://gerrit.lix.systems/c/lix/+/5370)

  As Lix uses the path `<nix/fetchurl.nix>` for bootstrapping purposes, the ability to shadow it by adding `nix=/some/path` (or `/other/path` that contains a `nix` directory) to the search path is not desirable.

  Lix 2.95 deprecated this behavior with a warning, Lix 2.96 now turns it into a hard error if the `nix-path-shadow` deprecated feature isn't enabled. This deprecated feature is slated to be removed in Lix 2.98.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht), [jade](https://git.lix.systems/jade), and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Changes to `flake.nix` validation [nix#4945](https://github.com/NixOS/nix/issues/4945) [cl/5523](https://gerrit.lix.systems/c/lix/+/5523)

  Flakes try to keep their inputs and metadata "simple", to make sure no unbounded computation may happen when calling e.g. `nix flake show`.
  Those checks were haphazard, a maintenance burden, and also easily circumventable.

  Lix has now replaced all the old checks by a simple rule: **No function calls outside of `outputs`.**
  This is easier to reason about than the previous set of inconsistent rules, and crucially now also allows syntax features that users felt like they *should* have worked in the past, like let bindings.
  However, some warts still remain for now: Some syntax constructs like `-1` internally desugar to `__sub 0 1`, which is a function call and thus remains forbidden.
  This will be rectified as soon as the deprecation period of the respective anti-features has been completed.

  This change is **breaking** in the sense that flakes which are written with the newly allowed language features will not evaluate with an older Lix version which still uses the old, more restrictive checks.
  Crucially, this also affects **all transitive dependants** of such Flakes.

  Many thanks to [piegames](https://git.lix.systems/piegames), [Qyriad](https://git.lix.systems/Qyriad), and [eldritch horrors](https://git.lix.systems/pennae) for this.


## Features

- allow setting nested attributes via `--arg`/`--argstr` [lix#496](https://git.lix.systems/lix-project/lix/issues/496) [cl/5338](https://gerrit.lix.systems/c/lix/+/5338)

  Passing `--arg config.allowUnfree true` to e.g. `nix-build` now results in `config` with value
  `{ allowUnfree = true; }` passed to the expression.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Use mimalloc for faster evaluation [cl/5645](https://gerrit.lix.systems/c/lix/+/5645)

  Lix now links with [mimalloc](https://github.com/microsoft/mimalloc),
  replacing the system's default `malloc()` for all non-GC allocations.

  This yields a **5–12% wall-clock improvement** on evaluation workloads,
  ranging from `nix-instantiate hello` to `nix-env -qa` and full NixOS
  configurations.

  The allocator can be disabled at build time with `-Dmimalloc=disabled`,
  or by passing the `useMimalloc = false` override to the `lix` package.

  Many thanks to [Seth Flynn](https://git.lix.systems/getchoo) and [lovesegfault](https://github.com/lovesegfault) for this.

- nix-eval-jobs support `--apply` flag [lix#1214](https://git.lix.systems/lix-project/lix/issues/1214) [cl/5748](https://gerrit.lix.systems/c/lix/+/5748)

  `nix-eval-jobs` now supports the `--apply` flag. With this you can apply the
  provided function to the each derivation, the result of this function will then
  be serialized as a JSON value and stored inside `"extraValue"` key of the json
  line output.

  Many thanks to [isabelroses](https://git.lix.systems/isabelroses), [mic92](https://github.com/mic92), and [ysndr](https://github.com/ysndr) for this.

- nix-eval-jobs support `--select` flag [cl/5730](https://gerrit.lix.systems/c/lix/+/5730)

  `nix-eval-jobs` now supports the `--select` flag.
  This flag allows to specify a function that is applied against the evaluation root.
  This is applied before any attribute traversal begins.

  Many thanks to [mic92](https://github.com/mic92) for this.

- Improve nix doctor [cl/5316](https://gerrit.lix.systems/c/lix/+/5316) [cl/5317](https://gerrit.lix.systems/c/lix/+/5317) [cl/5318](https://gerrit.lix.systems/c/lix/+/5318) [cl/5319](https://gerrit.lix.systems/c/lix/+/5319) [cl/5320](https://gerrit.lix.systems/c/lix/+/5320) [cl/5768](https://gerrit.lix.systems/c/lix/+/5768) [cl/5829](https://gerrit.lix.systems/c/lix/+/5829)

  The `nix doctor` diagnosics interface now provides a lot more useful information including, but not limited to:
  - General system information (OS, Hardware etc)
  - Nix Information like Sandbox, Version, Store, State and other directories
  - Flake registry
  - Search path Information
  - Nixpkgs provenance
  - Remote builder configuration (including remote connection)
  - fix crash when having relative Paths in PATH

  Many thanks to [rootile (Rutile)](https://git.lix.systems/rootile) and [Raito Bezarius](https://git.lix.systems/raito) for this.

- `ssh-ng` stores support build options and are now considered stable [cl/6415](https://gerrit.lix.systems/c/lix/+/6415) [cl/6416](https://gerrit.lix.systems/c/lix/+/6416)

  `ssh-ng` stores now support the options `build-timeout`, `max-silent-time`, and `keep-failed` when used with a compatible Lix.
  This brings build option support to parity with `ssh` stores, particularly for remote builds.

  Both involved Lixes must be 2.96 or newer for this to work.
  An older Lix will never send these options, and a newer Lix will detect whether the remote advertises support for this feature and only then send these options.

  We now also consider `ssh-ng` stores as **stable, not experimental** any more.
  The protocol has seen sufficient testing and the last remaining feature differences we know of are now resolved.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Support zstd and bz2 tarballs for Nix expressions in channels references [lix#1252](https://git.lix.systems/lix-project/lix/issues/1252) [cl/5853](https://gerrit.lix.systems/c/lix/+/5853) [cl/5855](https://gerrit.lix.systems/c/lix/+/5855)

  Lix has always supported referencing NixOS channels using the `channel:`
  syntax, e.g. `channel:nixos-26.05`. These references can be used anywhere
  a source URL is expected, such as in `-I` flags with a Nix command or `url
  = "channel:nixos-26.05";` in a `fetchTarball` call or even with the
  `nix-channel` command.

  Previously, such references resolved to a stable channel URL
  (`https://channels.nixos.org/$VERSION`) and appended `nixexprs.tar.xz`. As the
  Nixpkgs infrastructure has begun publishing `nixexprs.tar.zst` ([Zstandard](https://github.com/facebook/zstd))
  files alongside existing formats, Lix now attempts to fetch tarballs in the
  following priority order:

  - `nixexprs.tar.zst` (Zstandard)
  - `nixexprs.tar.xz` (XZ)
  - `nixexprs.tar.bz2` (Bzip2)

  This ensures that users can download historical and modern channel artifacts without any
  change in their Nix code.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [eldritch horrors](https://git.lix.systems/pennae) for this.


## Improvements

- add time complexity to various `builtins` [cl/5928](https://gerrit.lix.systems/c/lix/+/5928)

  Add time complexity to various `builtins`.

  Many thanks to [isabelroses](https://git.lix.systems/isabelroses), [xokdvium](https://github.com/xokdvium), and [hsjobeki](https://github.com/hsjobeki) for this.

- check for missing ca-file or netrc-file if one is specified [lix#1106](https://git.lix.systems/lix-project/lix/issues/1106) [cl/5646](https://gerrit.lix.systems/c/lix/+/5646)

  If the settings `ssl-cert-file` or `netrc-file` have been set by the user, check if those files actually exist and fail if they are missing.

  Many thanks to [Astreaprtcl](https://git.lix.systems/astreaprtcl) for this.

- Allow moving between stack frames relative to current debugger frame [lix#1156](https://git.lix.systems/lix-project/lix/issues/1156) [cl/5411](https://gerrit.lix.systems/c/lix/+/5411)

  Debugging functional programs often involve switching between a bunch of stack
  frames to get the full context of what's happening and who's calling who.
  Before this change, going up or down the stack in the nix debugger with `:st`
  meant remembering the absolute index of each stack frame, instead of their
  positions relative to one another; this got tiring *fast*.

  Now, you can prepend `:st`'s argument with a + or - sign to indicate you want to
  move relative to the current stack frame. For example, typing `:st +3` when you
  were on frame `10` will go frame `13`; vice-versa, typing `:st -4` on frame `6`
  will go to frame `2`.

  Many thanks to [blokyk](https://github.com/blokyk) for this.

- Print REPL backtraces in more convenient order [cl/5491](https://gerrit.lix.systems/c/lix/+/5491)

  When using the debugger, stack traces printed with the `:bt` command were
  previously printed in reverse order compared to most other situations where they
  appeared: the current stack frame would be printed at the very top, with the
  most outer frame at the bottom, meaning that you'd have to scroll up to get a
  sense of where you are.

  With this change, the stack frames are printed such that the most relevant ones
  are immediatly visible at the bottom, just like other traces in lix (e.g.
  ones caused by errors).

  Many thanks to [blokyk](https://github.com/blokyk) for this.

- invalid arguments to :st now print an error [cl/5386](https://gerrit.lix.systems/c/lix/+/5386)

  When using the debugger, the `:st` command used to traverse the call stack would
  silently fail and put the debugger in an invalid state if the argument given to
  it wasn't a valid stack frame index.

  This change adds an error message warning the user if the given index wasn't a
  valid frame (telling them the range of valid indices), as well as if it wasn't
  even a valid integer to begin with.

  Many thanks to [blokyk](https://github.com/blokyk) for this.

- REPL now uses rustyline [cl/5703](https://gerrit.lix.systems/c/lix/+/5703)

  The REPL now uses [rustyline](https://github.com/kkawakam/rustyline) for input processing instead
  of editline. This comes with some improvements to REPL behavior: wrapping lines no longer confuse
  the line editor, unicode is fully supported, pasting multiline expressions is noew possible, even
  undo commands are now available! We plan to improve the REPL further using these newfound powers.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Always print frames from `addErrorContext` in error traces [cl/5847](https://gerrit.lix.systems/c/lix/+/5847)

  The [`builtins.addErrorContext`](@docroot@/language/builtins.md#builtins-addErrorContext)
  function allows an author to add artificial stack frames with custom messages to
  help end-users understand the context of an error and the path the code took to
  get there, without having to read and understand the original source code. A
  particularly notable user of this is the Nixpkgs module system, which adds
  custom frames detailing what option it's evaluating or which definition it's
  looking at.

  However, previously, these frames would end up treated just as any other,
  meaning they would most often not be visible without `--show-trace`; yet, using
  `--show-trace`, they would be drowned out in the noise of the hundreds of other
  frames, rendering them just as unusable.

  With this change, these frames are now unconditionally shown, even without
  `--show-trace`, which makes basic error traces much more informative.

  Many thanks to [blokyk](https://github.com/blokyk) for this.


## Fixes

- Forbid unprivileged `repair` through the daemon

  The daemon's `AddToStore`, `AddToStoreNar` and `AddMultipleToStore` operations
  accepted the client-supplied `repair` flag even from untrusted clients. Because
  `LocalStore` does not repair paths atomically, an untrusted client could
  replace the contents of an arbitrary store path with attacker-controlled data
  while the path remained registered as valid in the database, leading to local
  privilege escalation.

  `repair` is now rejected for untrusted clients, matching the existing
  restrictions on `BuildPaths`, `BuildPathsWithResults` and `VerifyStore`.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- `nix-hash` can no longer truncate SRI hashes [cl/6348](https://gerrit.lix.systems/c/lix/+/6348)

  `nix-hash` was able to truncate SRI hashes using its `--truncate` flag, creating invalid SRI hashes in the process. This is no longer allowed.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- don't treat tarball fetches with empty or zero hash as locked [lix#1233](https://git.lix.systems/lix-project/lix/issues/1233) [cl/5808](https://gerrit.lix.systems/c/lix/+/5808)

  Lix no longer treats tarball fetches with empty or zero hashes as locked.
  All such fetches are now also affected by `tarball-ttl` as a consequence.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- builtins.break doesn't break expression anymore [lix#1165](https://git.lix.systems/lix-project/lix/issues/1165) [cl/5422](https://gerrit.lix.systems/c/lix/+/5422)

  Wrapping an expression in `builtins.break` used to break some builtins like
  `map` and the `is*` functions, which could modify the execution path of code
  inadvertently, made debugging nix harder than it already is, and in some cases
  even crashed the interpreter. Now, using `break` should be completely
  transparent to whatever function receives it as an input, preventing the
  above-mentioned issues.

  Many thanks to [blokyk](https://github.com/blokyk) for this.

- flake config warnings are now printed to stderr [lix#1155](https://git.lix.systems/lix-project/lix/issues/1155) [cl/5379](https://gerrit.lix.systems/c/lix/+/5379)

  The settings listed in a flake-config confirmation prompt are now printed to stderr rather than stdout, which allows `nix print-dev-env` to emit valid bash again even in the presence of untrusted settings.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Use a lock when fetching inputs [lix#1122](https://git.lix.systems/lix-project/lix/issues/1122) [cl/5438](https://gerrit.lix.systems/c/lix/+/5438)

  Up to now, attempting to fetch the same git input from multiple processes
  concurrently when the input is not yet cached presented multiple issues:

  - If the input was not already present, it would unnecessarily be fetched
    multiple times;

  - Access to the fetcher cache database was contentious, and could lead to
    evaluation or flake locking failing unnecessary because the fetcher cache
    was locked.

  We now acquire a lock on a path based on a hash of the input specification
  before accessing the fetcher db, reducing contention significantly, and
  preventing more than one process from fetching the same path at the same time.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Fix `nix-copy-closure --include-outputs` [nix#5105](https://github.com/NixOS/nix/issues/5105) [cl/5588](https://gerrit.lix.systems/c/lix/+/5588)

  The `--include-outputs` flag for `nix-copy-closure` now works as intended.
  Previously, the option was accepted but silently ignored.

  Many thanks to [Erik Jensen](https://git.lix.systems/rkjnsn) for this.

- Hash mismatch diagnostics now work with `structuredAttrs` [lix#1175](https://git.lix.systems/lix-project/lix/issues/1175) [cl/5441](https://gerrit.lix.systems/c/lix/+/5441)

  Nixpkgs fetchers like `fetchurl` now use `structuredAttrs`, which broke the
  hash mismatch diagnostics added in Lix 2.91. This has been fixed and the likely
  URL is now shown again.

  Many thanks to [keysmashes](https://github.com/keysmashes) for this.

- Subdaemons gets overridden settings from their parent daemon [lix#1071](https://git.lix.systems/lix-project/lix/issues/1071) [lix#1270](https://git.lix.systems/lix-project/lix/issues/1270) [cl/6525](https://gerrit.lix.systems/c/lix/+/6525)

  Since 1e8f7c7c7, subdaemons have ceased to receive overridden settings from
  their parent daemon, i.e. set by the CLI and not the configuration file.

  This was not remarked because we have migrated most of the Lix users to
  systemd-spawned subdaemons where the override is applied at the systemd
  template unit.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Fix unsigned overflow leading to out-of-band write in the NAR parser [cl/5554](https://gerrit.lix.systems/c/lix/+/5554)

  The NAR parser contained an unsigned integer overflow that could be used by an
  attacker to write arbitrary data to an unknown memory location and possibly
  achieve code execution. A successful attack on the system-wide Lix daemon
  could lead to privilege escalation to root. Any process that involves NAR
  serialization could trigger this issue, including (but not limited to)

    - local user interaction, whether the users are trusted or untrusted
    - malicious substituters sending malformed NARs
    - remote builders sending malformed build results
    - remote daemons sending malformed inputs when requesting remote builds

  Successful attacks using this bug require ASLR weakening of some sort, whether
  by architecture constraints (e.g. on 32 bit systems, where little randomization
  is possible) or system configuration (e.g. low ASLR entropy when loading
  libraries), and millions of attempts. Local attacks can be mounted in less than
  an hour. Remote builds typically require a fresh SSH connection for each build
  and are thus less susceptible. Only one attempt can be made by substituters for
  every build using substituters, they are thus not a likely vector for attacks.

  At the time of writing, MITRE has not assigned this a CVE yet.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae), [Raito Bezarius](https://git.lix.systems/raito), [edef](https://github.com/edef1c), and [sandydoo](https://github.com/sandydoo) for this.


## Packaging

- Lix now requires lowdown 1.4.0 or later [cl/5374](https://gerrit.lix.systems/c/lix/+/5374)

  Support for linking against `lowdown < 1.4.0` has been removed from Lix since
  all supported Nixpkgs channels distribute lowdown 2.0.4 or later.

  Many thanks to [sterni](https://git.lix.systems/sterni) for this.


## Miscellany

- libexpr: allow empty attr-names in parseAttrPath if they are quoted [cl/5375](https://gerrit.lix.systems/c/lix/+/5375)

  Empty strings are now allowed in attribute paths as consumed by e.g. `nix-build`.
  I.e. `nix-build -A 'foo."".bar'` works now.
  The quotes are necessary, i.e. `nix-build -A foo..bar` will throw an error.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- `nix realisation` command removed [cl/6421](https://gerrit.lix.systems/c/lix/+/6421)

  The `nix realisation` command has been removed.
  It was exclusive to the (previously removed) CA derivations feature and had only thrown errors when used.
  They merely throw different errors now.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Remove `max-connections` store parameters for `ssh://` and `ssh-ng://` stores [cl/5895](https://gerrit.lix.systems/c/lix/+/5895)

  The `max-connections` parameter was undocumented, untested, and (in the case of `ssh`) even ignored
  entirely for remote builds. During a survey of public nixos configurations we have found *two* uses
  of `max-connections` for `ssh-ng`, and none at all for `ssh`. Since it is so rarely used but brings
  significant internal complexity that hinders improvements we have decided to remove these features.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Remove the `daemon-trust-override` experimental feature [cl/6071](https://gerrit.lix.systems/c/lix/+/6071)

  The `daemon-trust-override` experimental feature has been removed. It provided no large benefit over
  custom configuration using e.g. the `NIX_CONFIG` enviroment variable and was only used in our tests.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
