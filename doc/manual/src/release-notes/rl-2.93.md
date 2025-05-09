# Lix 2.93 "Bici Bici" (2025-05-09)


# Lix 2.93.0 (2025-05-09)
## Breaking Changes

- more deprecated features

  This release cycle features a new batch of deprecated (anti-)features.
  You can opt in into the old behavior with `--extra-deprecated-features` or any equivalent configuration option.

  - `cr-line-endings`: Current handling of CR (`\r`) or CRLF (`\r\n`) line endings in Nix is inconsistent and broken, and will lead to unexpected evaluation results with certain strings. Given that fixing the semantics might silently alter the evaluation result of derivations, the only option at the moment is to disallow them altogether. More proper support for CRLF is planned to be added back again in the future. Until then, all files must use `\n` exclusively.
  - `nul-bytes`: Currently the Nix grammar allows NUL bytes (`\0`) in strings, and thus indirectly also in identifiers. Unfortunately, several core parts of the code base still work with NUL-terminated strings and cannot easily be migrated. Also note that it is still possible to introduce NUL bytes and thus problematic behavior via other means, those are tracked separately.

  Many thanks to [piegames](https://git.lix.systems/piegames) and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Removal of the `recursive-nix` experimental feature [fj#767](https://git.lix.systems/lix-project/lix/issues/767) [cl/2872](https://gerrit.lix.systems/c/lix/+/2872)

  The `recursive-nix` experimental feature and all associated code have been removed.

  `recursive-nix` enabled running Nix operations (like evaluations and builds) *inside* a derivation builder. This worked by spawning a temporary Nix daemon socket within the build environment, allowing the derivation to emit outputs that appeared in the outer store. This was primarily used to prototype **dynamic derivations** (dyndrvs), where build plans are generated on-the-fly during a build.

  However, this approach introduced critical issues:

  - It entrenched the legacy Nix daemon protocol as part of the derivation ABI, which is a blocker for future stabilization.
  - It imposed tight coupling between sandbox setup code and knowledge of Nix internals, complicating refactoring and long-term maintenance.
  - It was never intended to be the final design for dynamic derivations. The original Nix implementation team, who are leading dyndrv development, have agreed it will be replaced (likely via `varlink` or similar) before any stabilization.
  - There is currently no known usage of `recursive-nix` on `lix` or elsewhere **in production**.

  If you're using `recursive-nix` for something niche or experimental, we'd love to hear from you on the RFD issue.
  You can still run `nix` inside a builder manually if needed — including with isolated user namespaces and fake stores — but the special daemon-handshake machinery is gone.

  This removal unblocks several important internal cleanups.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Flake inputs/`builtins.fetchTree` invocations with `type = "file"` now have consistent (but different from previous versions) resulting paths [fj#750](https://git.lix.systems/lix-project/lix/issues/750) [cl/2864](https://gerrit.lix.systems/c/lix/+/2864)

  Previously `fetchTree { type = "file"; url = "...", narHash = "sha256-..."; }` could return a different result depending on whether someone has run `nix store add-path --name source ...` on a path with the same `narHash` as the flake input/`fetchTree` invocation (or if such a path exists in an accessible binary cache).

  In the past `type = "file"` flake inputs were, in contrast to all other flake inputs, hashed in *flat* hash mode rather than *recursive* hash mode.
  The difference between the two is that *flat* mode hashes are just what you get from `sha256sum` of a single file, whereas *recursive* hashes are the SHA256 sum of a NAR (Nix ARchive, a deterministic tarball-like format) of a file tree.

  Much of flakes assumes that everything is recursive-hashed including `nix flake archive`, substitution of flake inputs from binary caches, and more, which led to the substitution path code being taken if such a path is present, yielding a different store path non-deterministically.

  To fix this non-deterministic evaluation bug, we needed to break derivation hash stability, so some Nix evaluations now produce different results than previous versions of Lix.
  Lix now has consistent behaviour with CppNix 2.24 with respect to `file` flake inputs: they are *always* recursively hashed.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Builders are always started in a fresh cgroup namespace [cl/1996](https://gerrit.lix.systems/c/lix/+/1996)

  If you haven't enabled the experimental `cgroups` feature, Nix previously launched builder processes in new namespaces but did not create new cgroup namespaces. As a result, derivations could access and observe the parent cgroup namespace.

  Although this update introduces a breaking change, it ensures that all derivations now start in a fresh cgroup namespace by default. This reduces potential impurities observable within the sandbox, improving the likelihood of reproducible builds across different environments.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- `nix-instantiate --parse` outputs json [fj#487](https://git.lix.systems/lix-project/lix/issues/487) [nix#11124](https://github.com/NixOS/nix/issues/11124) [nix#4726](https://github.com/NixOS/nix/issues/4726) [nix#3077](https://github.com/NixOS/nix/issues/3077) [cl/2190](https://gerrit.lix.systems/c/lix/+/2190)

  `nix-instantiate --parse` does not print out the AST in a Nix-like format anymore.
  Instead, it now prints a JSON representation of the internal expression tree.
  Tooling should not rely on the stdout of `nix-instantiate --parse`.

  We've done our best to ensure that the new behavior is as compatible with the old one as possible.
  If you depend on the old behavior in ways that are not covered anymore or are otherwise negatively affected by this change,
  then please reach out so that we can find a sustainable solution together.

  Many thanks to [piegames](https://git.lix.systems/piegames) and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Remove experimental repl-flake [gh#10103](https://github.com/NixOS/nix/issues/10103) [fj#557](https://git.lix.systems/lix-project/lix/issues/557) [gh#10299](https://github.com/NixOS/nix/pull/10299) [cl/2147](https://gerrit.lix.systems/c/lix/+/2147)

  The `repl-flake` experimental feature flag has been removed, its functionality is now the default when `flakes` experimental feature is active. The `nix repl` command now works like the rest of the new CLI in that `nix repl {path}` now tries to load a flake at `{path}` (or fails if the `flakes` experimental feature isn't enabled).

  Many thanks to [Jonathan De Troye](https://github.com/detroyejr) and [KFears](https://git.lix.systems/kfearsoff) for this.


## Features

- `lix foo` now invokes `lix-foo` from PATH [cl/2119](https://gerrit.lix.systems/c/lix/+/2119)

  Lix introduces the ability to extend the Nix command line by adding custom
  binaries to the `PATH`, similar to how Git integrates with other tools. This
  feature allows developers and end users to enhance their workflow by
  integrating additional functionalities directly into the Nix CLI.

  #### Examples

  For example, a user can create a custom deployment tool, `lix-deploy-tool`, and
  place it in their `PATH`. This allows them to execute `lix deploy-tool`
  directly from the command line, streamlining the process of deploying
  applications without needing to switch contexts or use separate commands.

  #### Limitations

  For now, autocompletion is supported to discover new custom commands, but the
  documentation will not render them. Argument autocompletion of the custom
  command is not supported either.

  This is also locked behind a new experimental feature called
  `lix-custom-sub-commands` to enable developing all the required features.

  Only the top-level `lix` command can be extended, this is an artificial
  limitation for the time being until we flesh out this feature.

  #### Outline

  In the future, this feature may pave the way for moving the Flake subcommand
  line to its own standalone binary, allowing for a more focused approach to
  managing Nix Flakes while letting the community explore alternatives to
  dependency management.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- `nix-env --install` now accepts a `--priority` flag [cl/2607](https://gerrit.lix.systems/c/lix/+/2607)

  `nix-env --install` now has an optional `--priority` flag.

  Previously, it was only possible to specify a priority by adding a
  `meta.priority` attribute to a derivation. `meta` attributes only exist during
  eval, so that wouldn't work for installing a store path. It was also possible
  to change a priority after initial installation using `nix-env --set-flag`,
  however if there is already a conflict that needs to be resolved via priorities,
  this will not work.

  Now, a priority can be set at install time using `--priority`, which allows for
  cleanly overriding the priority at install time.

  #### Example

  ```console
  $ nix-build
  $ nix-env --install --priority 100 ./result
  ```

  Many thanks to [Andrew Hamon](https://github.com/andrewhamon) for this.

- Add support for eBPF USDT/dtrace probes inside Lix [fj#727](https://git.lix.systems/lix-project/lix/issues/727) [cl/2884](https://gerrit.lix.systems/c/lix/+/2884)

  eBPF tracers like `bpftrace` and `dtrace` are a group of similar tools for debugging production systems.
  User-space statically defined tracing probes (USDT) allow for defining zero or near-zero disabled-probe-effect probes, thus allowing instrumentation of hot paths in production builds.
  Lix now has internal support for defining these probes and has shipped its first probe.

  As of this writing it is available by default in the Linux build of Lix.

  To try it out on Linux, you can use the following example command:

  ```
  $ sudo bpftrace -l 'usdt:/path/to/liblixstore.so:*:*'
  usdt:/path/to/liblixstore.so:lix_store:filetransfer__read

  $ sudo bpftrace -e 'usdt:*:lix_store:filetransfer__read { printf("%s read %d\n", str(arg0), arg1); }'
  Attaching 1 probe...
  https://cache.nixos.org/wvpzaycmvs39h5bcsfrxkjsg48mj4h73.narinf.. read 8192
  https://cache.nixos.org/wvpzaycmvs39h5bcsfrxkjsg48mj4h73.narinf.. read 8192
  https://cache.nixos.org/nar/1qshsc30nlarzdig0v9b1aasdkwaxhnv0a0.. read 65536
  https://cache.nixos.org/nar/1qshsc30nlarzdig0v9b1aasdkwaxhnv0a0.. read 65536
  ```

  Note that bpftrace does not offer any way to list the arguments to USDT probes in a human readable form.
  To get the probe definitions, see the `*.d` files in the Lix source code, for example, `lix/libstore/trace-probes.d`.

  For more resources on eBPF/bpftrace and dtrace, see:
  * The book "BPF Performance Tools" by Brendan Gregg, which discusses bpftrace at length.
  * <https://ebpf.io/get-started/>
  * [Illumos' dtrace book](https://illumos.org/books/dtrace/preface.html)

  Many thanks to [jade](https://git.lix.systems/jade) for this.


## Improvements

- Always print `post-build-hook` logs [fj#675](https://git.lix.systems/lix-project/lix/issues/675) [cl/2801](https://gerrit.lix.systems/c/lix/+/2801)

  Logs of `post-build-hook` are now printed unconditionally.
  They used to be tied to whether print-build-logs is set, which made debugging them a nightmare when they fail, since the failure output would be eaten if build logs are disabled.
  Most usages of `post-build-hook` are pretty quiet especially compared to build logs, so it should not be that bothersome to not be able to turn off.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Crashes land in syslog now [cl/2640](https://gerrit.lix.systems/c/lix/+/2640)

  When Lix crashes with unexpected exceptions and in some other conditions, it prints bug reporting instructions.
  Previously, these only landed in stderr and not in syslog.
  However, on larger Lix installations, it may be the case that Lix crashes in the client without the logs landing in the system logs, which impeded diagnosis.

  Now, such crashes always land in syslog too.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Deletion of specific paths no longer fails fast [cl/2778](https://gerrit.lix.systems/c/lix/+/2778)

  `nix-store --delete` and `nix store delete` now continue deleting
  paths even if some of the given paths are still live. An error is only
  thrown once deletion of all the given paths has been
  attempted. Previously, if some paths were deletable and others
  weren't, the deletable ones would be deleted iff they preceded the
  live ones in lexical sort order.

  The error message for still-live paths no longer reports the paths
  that could not be deleted, because there could potentially be many of
  these.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- `--skip-live` for path deletion [cl/2778](https://gerrit.lix.systems/c/lix/+/2778)

  `nix-store --delete` and `nix store delete` now support a
  `--skip-live` option and a `--delete-closure` option.

  This makes custom garbage-collection logic a lot easier to implement
  and experiment with:

  - Paths known to be large can be thrown at `nix store delete` without
    having to manually filter out those that are still reachable from a
    root, e.g.
    `nix store delete /nix/store/*mbrola-voices*`

  - The `--delete-closure` option allows extending this to paths that are
    not large themselves but do have a large closure size, e.g.
    `nix store delete /nix/store/*nixos-system-gamingpc*`.

  - Other heuristics like atime-based deletion can be applied more
    easily, because `nix store delete` once again takes over the task of
    working out which paths can't be deleted.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Allow `nix store diff-closures` to output JSON [cl/2360](https://gerrit.lix.systems/c/lix/+/2360)

  Add the `--json` option to the `nix store diff-closures` command to allow users to collect diff information into a machine readable format.

  ```bash
  $ build/lix/nix/nix store diff-closures --json /run/current-system /nix/store/n1prick95pihd4lkv58nn3pzg1yivcdb-neovim-0.10.4/bin/nvim | jq | head -n 23
  {
    "packages": {
      "02overridedns": {
        "sizeDelta": -688,
        "versionsAfter": [],
        "versionsBefore": [
          ""
        ]
      },
      "50-coredump.conf": {
        "sizeDelta": -1976,
        "versionsAfter": [],
        "versionsBefore": [
          ""
        ]
      },
      "Diff": {
        "sizeDelta": -514864,
        "versionsAfter": [],
        "versionsBefore": [
          "0.4.1"
        ]
      },
  ```

  Many thanks to [Xavier Maso](https://github.com/pamplemousse) for this.

- Show all missing and unexpected arguments in erroneous function calls [cl/2477](https://gerrit.lix.systems/c/lix/+/2477)

  When calling a function that expects an attribute set, lix will now show all
  missing and unexpected arguments.
  e.g. with `({ a, b, c } : a + b + c) { a = 1; d = 1; }` lix will now show the error:
  ```
  [...]
  error: function 'anonymous lambda' called without required arguments 'b' and 'c' and with unexpected argument 'd'
  [...]
  ```
  Previously lix would just show `b`.
  Furthermore lix will now only suggest arguments that aren't yet used.
  e.g. with `({ a?1, b?1, c?1 } : a + b + c) { a = 1; d = 1; e = 1; }` lix will now show the error:
  ```
  [...]
  error: function 'anonymous lambda' called with unexpected arguments 'd' and 'e'
         at «string»:1:2:
              1| ({ a?1, b?1, c?1 } : a + b + c) { a = 1; d = 1; e = 1; }
               |  ^
         Did you mean one of b or c?
  ```
  Previously lix would also suggest `a`.
  Suggestions are unfortunately still currently just for the first missing argument.

  Many thanks to [Zitrone](https://git.lix.systems/quantenzitrone) for this.

- REPL improvements [cl/2319](https://gerrit.lix.systems/c/lix/+/2319) [cl/2320](https://gerrit.lix.systems/c/lix/+/2320) [cl/2321](https://gerrit.lix.systems/c/lix/+/2321)

  The REPL has seen various minor improvements:

  - Variable declarations have been improved, making copy-pasting code from attrsets a lot easier:
    - Declarations can now optionally end with a semicolon
    - Multiple declarations can be done within one command, separated by semicolon
    - The `foo.bar = "baz";` syntax from attrsets is also supported, however without the attrset merging rules and with restrictions on dynamic attrs like in `let` bindings.
    - Variable names now use the proper Nix grammar rules, instead of a regex that only vaguely matched legal identifiers.
  - Better error messages overall
  - The `:env` command to print currently available variables now also works outside of debug mode
  - Adding variables to the REPL now prints a small message on success

  Many thanks to [piegames](https://git.lix.systems/piegames) for this.

- Consistently use SRI hashes in hash mismatch errors [cl/2868](https://gerrit.lix.systems/c/lix/+/2868)

  Previously there were a few weird cases (flake inputs, e.g., among others) where Lix would print the old Nix base-32 hash format (sha256:abcd...) rather than the newer [SRI base64 format](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) (sha256-AAAA...) that is used in most Lix hash mismatch errors.
  This made it annoying to compare them to hashes shown by most of the modern UI surface of Lix which uses SRI.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Allow specifying ports for remote ssh[-ng] stores [cl/2432](https://gerrit.lix.systems/c/lix/+/2432)

  You can now specify which port should be used for a remote ssh store (e.g. for remote/distributed builds) through a uri parameter.
  E.g., when a remote builder `foo` is listening on port `1234` instead of the default, it can be specified like this `ssh://foo?port=1234`.

  Many thanks to [seppel3210](https://github.com/Seppel3210) for this.

- Implicit `__toString` now have stack trace entries [cl/3055](https://gerrit.lix.systems/c/lix/+/3055)

  Coercion of attribute sets to strings via their `__toString` attribute now produce stack
  frames pointing to the coercion site and the attribute definition. This makes locating a
  coercion function error easier as the fault location is now more likely to be presented.

  Previously:
  ```
  nix-repl> builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
  error:
         … while calling the 'substring' builtin
           at «string»:1:1:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               | ^

         … caused by explicit throw
           at «string»:1:48:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               |                                                ^

         error: bar
  ```

  Now:
  ```
  nix-repl> builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
  error:
         … while calling the 'substring' builtin
           at «string»:1:1:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               | ^

         … while converting a set to string
           at «string»:1:25:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               |                         ^

         … from call site
           at «string»:1:29:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               |                             ^

         … while calling '__toString'
           at «string»:1:42:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               |                                          ^

         … caused by explicit throw
           at «string»:1:48:
              1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
               |                                                ^

         error: bar
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.


## Fixes

- Avoid unnecessarily killing processes for the build user's UID [nix#9142](https://github.com/NixOS/nix/issues/9142) [fj#667](https://git.lix.systems/lix-project/lix/issues/667)

  We no longer kill all processes under the build user's UID before and after
  builds on Linux with sandboxes enabled.

  This avoids unrelated processes being killed. This might happen for instance,
  if the user is running Lix inside a container, wherein the build users use the same UIDs as the daemon's.

  Many thanks to [teofilc](https://git.lix.systems/teofilc) for this.

- Forbid impure path accesses in pure evaluation mode again [cl/2708](https://gerrit.lix.systems/c/lix/+/2708)

  Lix 2.92.0 mistakenly started allowing the access to ancestors of allowed paths in pure evaluation mode.
  This made it possible to bypass the purity restrictions, for example by copying arbitrary files to the store:
  ```nix
  builtins.path {
    path = "/";
    filter = …;
  }
  ```
  Restore the previous behaviour of prohibiting such impure accesses.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.

- Ctrl-C works correctly on macOS again [fj#729](https://git.lix.systems/lix-project/lix/issues/729) [cl/3066](https://gerrit.lix.systems/c/lix/+/3066)

  Due to a kernel bug in macOS's `poll(2)` implementation where it would forget about event subscriptions, our detection of closed connections in the Lix daemon didn't work and left around lingering daemon processes.
  We have rewritten that thread to use `kqueue(2)`, which is what the `poll(2)` implementation uses internally in the macOS kernel, so now Ctrl-C on clients will reliably terminate daemons once more.

  This FD close monitoring has had the highest Apple bug ID references per line of code anywhere in the project, and hopefully not using poll anymore will stop us hitting bugs in poll.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Fetch peer PID for daemon connections on macOS [fj#640](https://git.lix.systems/lix-project/lix/issues/640) [cl/2453](https://gerrit.lix.systems/c/lix/+/2453)

  `nix-daemon` will now fetch the peer PID for connections on macOS, to match behavior with Linux.
  Besides showing up in the log output line, If `nix-daemon` is given an argument (such as `--daemon`)
  that argument will be overwritten with the peer PID for the forked process that handles the connection,
  which can be used for debugging purposes.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.

- Test group membership better on macOS [gh#5885](https://github.com/NixOS/nix/issues/5885) [cl/2566](https://gerrit.lix.systems/c/lix/+/2566)

  `nix-daemon` will now test group membership better on macOS for `trusted-users` and `allowed-users`.
  It not only fetches the peer gid (which fixes `@staff`) but it also asks opendirectory for group
  membership checks instead of just using the group database, which means nested groups (like `@_developer`)
  and groups with synthesized membership (like `@localaccounts`) will work.

  Many thanks to [lilyball](https://git.lix.systems/lilyball) for this.

- `nix store delete` no longer builds paths [cl/2782](https://gerrit.lix.systems/c/lix/+/2782)

  `nix store delete` no longer realises the installables
  specified. Previously, `nix store delete nixpkgs#hello` would download
  hello only to immediately delete it again. Now, it exits with an error
  if given an installable that isn't in the store.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Fix nix-store --delete on paths with remaining referrers [cl/2783](https://gerrit.lix.systems/c/lix/+/2783)

  Nix 2.5 introduced a regression whereby `nix-store --delete` and `nix
  store delete` started to fail when trying to delete a path that was
  still referenced by other paths, even if the referrers were not
  reachable from any GC roots. The old behaviour, where attempting to
  delete a store path would also delete its referrer closure, is now
  restored.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Add a straightforward way to detect if in a Nix3 Shell [nix#6677](https://github.com/NixOS/nix/issues/6677) [nix#3862](https://github.com/NixOS/nix/issues/3862) [cl/2090](https://gerrit.lix.systems/c/lix/+/2090)

  Running `nix shell` or `nix develop` will now set `IN_NIX_SHELL` to
  either `pure` or `impure`, depending on whether `--ignore-environment`
  is passed. `nix develop` will always be an impure environment.

  Many thanks to [Ersei Saggi](https://github.com/9p4) for this.

- Fix experimental and deprecated features showing as integers in `nix config show --json` [fj#738](https://git.lix.systems/lix-project/lix/issues/738) [cl/2882](https://gerrit.lix.systems/c/lix/+/2882)

  Internal changes in 2.92 caused `nix config show --json` to show deprecated and experimental features not as the list of named features 2.91 and earlier produced, but as integers. This has been fixed.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- `builtins.fetchTree` is no longer visible in `builtins` when flakes are disabled [cl/2399](https://gerrit.lix.systems/c/lix/+/2399)

  `builtins.fetchTree` is the foundation of flake inputs and flake lock files, but is not fully specified in behaviour, which leads to regressions, behaviour differences with CppNix, and other unfun times.
  It's gated behind the `flakes` experimental feature, but prior to now, would throw an uncatchable error at runtime when used without the `flakes` feature enabled.
  Now it's like other builtins which are experimental feature gated, where it is not visible without the relevant feature enabled.

  This fixes a bug in using Eelco Dolstra's version of flake-compat on Lix (and a divergence with CppNix): https://github.com/edolstra/flake-compat/issues/66

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- fix usage of `builtins.filterSource` and `builtins.path` with the filter argument when using chroot stores [nix#11503](https://github.com/NixOS/nix/issues/11503)

  The semantics of `builtins.filterSource` (and the `filter` argument for
  `builtins.path`) have been adjusted regarding how paths inside the Nix store
  are handled.

  Previously, when evaluating whether a path should be included, the filtering
  function received the **physical path** if the source was inside the chroot store.

  Now, it receives the **logical path** instead.

  This ensures consistency in path handling and avoids potential
  misinterpretations of paths within the evaluator, which led to various fallouts
  mentioned in <https://github.com/NixOS/nixpkgs/pull/369694>.

  Many thanks to [lily](https://git.lix.systems/lilyinstarlight), [alois31](https://git.lix.systems/alois31), and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Fix `--help` formatting [fj#622](https://git.lix.systems/lix-project/lix/issues/622) [cl/2776](https://gerrit.lix.systems/c/lix/+/2776)

  The help printed when invoking `nix` or `nix-store` and subcommands with `--help` previously contained garbled terminal escapes. These have been removed.

  Many thanks to [lheckemann](https://git.lix.systems/lheckemann) for this.

- Parsing failures in flake.lock no longer crash Lix [fj#559](https://git.lix.systems/lix-project/lix/issues/559) [cl/2401](https://gerrit.lix.systems/c/lix/+/2401)

  Failure to parse `flake.lock` no longer hard-crashes Lix and instead produces a nice error message.

  ```
  error:
         … while updating the lock file of flake 'git+file:///Users/jade/lix/lix2'

         … while parsing the lock file at /nix/store/mm5dqh8a729yazzj82cjffxl97n5c62s-source//flake.lock

         error: [json.exception.parse_error.101] parse error at line 1, column 1: syntax error while parsing value - invalid literal;
   last read: '#'
  ```

  Many thanks to [gilice](https://git.lix.systems/gilice) for this.

- Flakes follow `--eval-system` where it makes sense [fj#673](https://git.lix.systems/lix-project/lix/issues/673) [fj#692](https://git.lix.systems/lix-project/lix/issues/692) [gh#11359](https://github.com/NixOS/nix/issues/11359) [cl/2657](https://gerrit.lix.systems/c/lix/+/2657)

  Most flake commands now follow `--eval-system` when choosing attributes to build/evaluate/etc.

  The exceptions are commands that actually run something on the local machine:
  - nix develop
  - nix run
  - nix upgrade-nix
  - nix fmt
  - nix bundle

  This is not a principled approach to cross compilation or anything, flakes still impede rather than support cross compilation, but this unbreaks many remote build use cases.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Remove some gremlins from path garbage collection [fj#621](https://git.lix.systems/lix-project/lix/issues/621) [fj#524](https://git.lix.systems/lix-project/lix/issues/524) [cl/2465](https://gerrit.lix.systems/c/lix/+/2465) [cl/2387](https://gerrit.lix.systems/c/lix/+/2387)

  Path garbage collection had some known unsoundness issues where it would delete things improperly and cause desynchronization between the filesystem state and the database state.
  Now Lix tolerates better if such a condition exists by not failing the entire GC if a path fails to delete.
  We also fixed a bug in our file locking implementation that is one possible root cause, but may not be every root cause.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) and [Raito Bezarius](https://git.lix.systems/raito) for this.

- Show illegal path references in fixed-outputs derivations [fj#530](https://git.lix.systems/lix-project/lix/issues/530) [cl/2726](https://gerrit.lix.systems/c/lix/+/2726)

  The error created when referencing a store path in a Fixed-Output Derivation is now more verbose, listing the offending paths.
  This allows for better pinpointing where the issue might be.

  An offender is the following derivation:

  ```nix
  pkgs.stdenv.mkDerivation {
    name = "illegal-fod";

    dontUnpack = true;
    dontBuild = true;

    installPhase = ''
      cp -R ${pkgs.hello} $out
    '';

    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
    outputHash = pkgs.lib.fakeHash;
  }
  ```

  The previous error shown would have been:

  ```
  error: illegal path references in fixed-output derivation '/nix/store/rpq4m1y79s2nhs1hj7k47yiyykxykiqa-illegal-fod.drv'
  ```

  and is now:

  ```
  error: the fixed-output derivation '/nix/store/rpq4m1y79s2nhs1hj7k47yiyykxykiqa-illegal-fod.drv' must not reference store paths but 2 such references were found:
           /nix/store/1q8w6gl1ll0mwfkqc3c2yx005s6wwfrl-hello-2.12.1
           /nix/store/wn7v2vhyyyi6clcyn0s9ixvl7d4d87ic-glibc-2.40-36
  ```

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- Show error when item from NIX_PATH cannot be downloaded

  For e.g. `nix-instantiate -I https://example.com/404`, you'd only get a warning if the download failed, such as

      warning: Nix search path entry 'https://example.com/404' cannot be downloaded, ignoring

  Now, the full error that caused the download failure is displayed with a note that the search
  path entry is ignored, e.g.

      warning:
           … while downloading https://example.com/404 to satisfy NIX_PATH lookup, ignoring search path entry

           warning: unable to download 'https://example.com/404': HTTP error 404 ()

           response body: […]

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Fix Lix crashing on invalid json [fj#642](https://git.lix.systems/lix-project/lix/issues/642) [fj#753](https://git.lix.systems/lix-project/lix/issues/753) [fj#759](https://git.lix.systems/lix-project/lix/issues/759) [fj#769](https://git.lix.systems/lix-project/lix/issues/769) [cl/2907](https://gerrit.lix.systems/c/lix/+/2907)

  Lix no longer crashes when it receives invalid JSON. Instead it'll point to the syntax error and give some context about what happened, for example

  ```
  ❯ nix derivation add <<<"""
  error:
         … while parsing a derivation from stdin

         error: failed to parse JSON: [json.exception.parse_error.101] parse error at line 2, column 1: syntax error while parsing value - unexpected end of input; expected '[', '{', or a literal
  ```

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Fix handling of `lastModified` in tarball inputs [cl/2792](https://gerrit.lix.systems/c/lix/+/2792)

  Previous versions of Lix would fail with the following error, if a
  [tarball flake input](@docroot@/protocols/tarball-fetcher.md) redirect
  to a URL that contains a `lastModified` field:

  ```
  error: input attribute 'lastModified' is not an integer
  ```

  This is now fixed.

  Many thanks to [xanderio](https://github.com/xanderio) and [Julian Stecklina](https://github.com/blitz) for this.

- Fix `--debugger --ignore-try` [cl/2440](https://gerrit.lix.systems/c/lix/+/2440)

  When in debug mode (e.g. from using the `--debugger` flag), enabling [`ignore-try`](@docroot@/command-ref/conf-file.md#conf-ignore-try) once again properly disables debug REPLs within [`builtins.tryEval`](@docroot@/language/builtins.md#builtins-tryEval) calls. Previously, a debug REPL would be started as if `ignore-try` was disabled, but that REPL wouldn't actually be in debug mode, and upon exiting the REPL the evaluating process would segfault.

  Many thanks to [Dusk Banks](https://git.lix.systems/bb010g) for this.

- Don't consider a path with a specified rev to be `locked` [cl/2064](https://gerrit.lix.systems/c/lix/+/2064)

  Until now it was allowed to do e.g.

      $ echo 'lalala' > testfile
      $ nix eval --expr '(builtins.fetchTree { path = "/home/ma27/testfile"; rev = "0000000000000000000000000000000000000000"; type = "path"; })'
      { lastModified = 1723656303; lastModifiedDate = "20240814172503"; narHash = "sha256-hOMY06A0ohaaCLwnhpZIMoAqi/8kG2vk30NRiqi0dfc="; outPath = "/nix/store/lhfz259iipmv9ky995rml8018jvriynh-source"; rev = "0000000000000000000000000000000000000000"; shortRev = "0000000"; }
      $ cat /nix/store/lhfz259iipmv9ky995rml8018jvriynh-source
      lalala

  because any kind of input with a `rev` specified is considered to be locked.

  With this change, inputs of type `path`, `indirect` and `tarball` are no longer
  considered locked with a rev, but no hash specified.

  This behavior was changed in
  [CppNix 2.21 as well](https://github.com/nixos/nix/commit/071dd2b3a4e6c0b2106f1b6f14ec26e153d97446) as well.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.

- Fix macOS sandbox profile size errors [fj#752](https://git.lix.systems/lix-project/lix/issues/752) [fj#718](https://git.lix.systems/lix-project/lix/issues/718) [cl/2861](https://gerrit.lix.systems/c/lix/+/2861)

  Fixed an issue on macOS where the sandbox profile could exceed size limits when building derivations with many dependencies. The profile is now split into multiple allowed sections to stay under the interpreter's limits.

  This resolves errors like

  ```
  error: (failed with exit code 1, previous messages: sandbox initialization failed: data object length 65730 exceeds maximum (65535)|failed to configure sandbox)

         error: unexpected EOF reading a line
  ```

  Many thanks to [Pierre-Etienne Meunier](https://github.com/P-E-Meunier) and [Poliorcetics](https://github.com/poliorcetics) for this.

- Fix interference of the multiline progress bar with output [cl/2774](https://gerrit.lix.systems/c/lix/+/2774)

  In some situations, the progress indicator of the multiline progress bar would interfere with persistent output.
  This would result in progress bar headers being visible in place of the desired text, for example the outputs shown after a `:b` command in the repl.
  The underlying ordering issue has been fixed, so that the undesired interference does not happen any more.

  Many thanks to [alois31](https://git.lix.systems/alois31) for this.

- Paralellise `nix store sign` using a thread pool [fj#399](https://git.lix.systems/lix-project/lix/issues/399) [cl/2606](https://gerrit.lix.systems/c/lix/+/2606)

  `nix store sign` with a large collection of provided paths (such as when using with `--all`) has historically
  signed these paths serially. Taking extreme amounts of time when preforming operations such as fixing binary
  caches. This has been changed. Now these signatures are performed using a thread pool like `nix store copy-sigs`.

  Many thanks to [Lunaphied](https://git.lix.systems/Lunaphied) for this.

- `post-build-hook` only receives settings that are set [fj#739](https://git.lix.systems/lix-project/lix/issues/739) [cl/2800](https://gerrit.lix.systems/c/lix/+/2800)

  If one is using `post-build-hook` to upload paths to a cache, it used to be broken if CppNix was used inside the script, since CppNix would fail about unsupported configuration option values in some of Lix's defaults.
  This is because `post-build-hook` receives the settings of the nix daemon in the `NIX_CONFIG` environment variable.
  Now Lix only emits overridden settings to `post-build-hook` invocations, which fixes this issue in the majority of cases: where the configuration is not explicitly incompatible.

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Remove lix-initiated ssh connection sharing [fj#304](https://git.lix.systems/lix-project/lix/issues/304) [fj#644](https://git.lix.systems/lix-project/lix/issues/644) [cl/3005](https://gerrit.lix.systems/c/lix/+/3005)

  Lix no longer explicitly requests ssh connection sharing (ControlMaster/ControlPath SSH
  options, see also ssh_config(5) man page) when connecting to remote stores.  This may
  impact command latency when `NIX_REMOTE` is set to a `ssh://` or `ssh-ng://` url, or if
  `--store` is specified. Remote build connections did not use ssh connection sharing.

  Connection sharing configuration is now inherited from user configuration at all times. It
  is now advisable to configure connection sharing for remote builders for improved latency.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.


## Development

- Add `nix_plugin_entry` entry point for plugins [fj#740](https://git.lix.systems/lix-project/lix/issues/740) [fj#359](https://git.lix.systems/lix-project/lix/issues/359) [gh#8699](https://github.com/NixOS/nix/pull/8699) [cl/2826](https://gerrit.lix.systems/c/lix/+/2826)

  Plugins are an exceptionally rarely used feature in Lix, but they are important as a prototyping tool for code destined for Lix itself, and we want to keep supporting them as a low-maintenance-cost feature.
  As part of the overall move towards getting rid of static initializers for stability and predictability reasons, we added an explicit `nix_plugin_entry` function like CppNix has, which is called immediately after plugin load, if present.
  This makes control flow more explicit and allows for easily registering things that have had their static initializer registration classes removed.

  Many thanks to [jade](https://git.lix.systems/jade) and [yorickvp](https://github.com/yorickvp) for this.


## Miscellany

- Set default of `connect-timeout` to `5` [cl/2799](https://gerrit.lix.systems/c/lix/+/2799)

  By default, the connection timeout to substituters is now 5s instead of 300s.
  That way, unavailable substituters are detected quicker.

  Many thanks to [ma27](https://git.lix.systems/ma27) for this.
