# Lix 2.95 "Kakigōri" (2026-03-13)


# Lix 2.95.0 (2026-03-13)
## Breaking Changes

- Deprecate shadowing internal files through the Nix search path [lix#998](https://git.lix.systems/lix-project/lix/issues/998) [cl/4632](https://gerrit.lix.systems/c/lix/+/4632)

  As Lix uses the path `<nix/fetchurl.nix>` for bootstrapping purposes, the ability to shadow it by adding `nix=/some/path` (or `/other/path` that contains a `nix` directory) to the search path is not desirable.

  To alleviate potential issues, Lix now emits a warning when the Nix search path contains potential shadows for internal files, which will be changed to an error in a future release.

  The warning can be disabled by enabling the deprecated feature `nix-path-shadow`.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- More deprecated features [cl/2092](https://gerrit.lix.systems/c/lix/+/2092) [cl/2310](https://gerrit.lix.systems/c/lix/+/2310) [cl/2311](https://gerrit.lix.systems/c/lix/+/2311) [cl/4638](https://gerrit.lix.systems/c/lix/+/4638) [cl/4652](https://gerrit.lix.systems/c/lix/+/4652) [cl/4764](https://gerrit.lix.systems/c/lix/+/4764)

  This release cycle features a new batch of deprecated (anti-)features.
  You can opt in into the old behavior with `--extra-deprecated-features` or any equivalent configuration option.

  - `broken-string-indentation` indented strings (those starting with  `''`) might produce unintended results due to how the whitespace stripping is done. Those cases will now warn the user.
  - `broken-string-escape` "escaped" characters without a properly defined escape sequence evaluate to "themselves". This is in most cases unintended behaviour, both for writing regexes, and using legacy or uncommon escape sequences like `\f`. The user will now be warned, if those are present.
  - `floating-without-zero` so far, one was able to declare a float using something like `.123`. This can cause confusion about accessing attributes. Floating point numbers must now always include the leading zero, i.e. `0.123`
  - `rec-set-merges` Attribute sets like `{ foo = {}; foo.bar = 42;}` implicitly merge at parse time, however if one of them is marked as recursive but not the others then the recursive attribute may get lost (order-dependent). Therefore, merging attrs with mixed-`rec` is now forbidden.
  - `rec-set-dynamic-attrs` Dynamic attributes have weird semantics in the presence of recursive attrsets (they evaluate *after* the rest of the set). This is now forbidden.
  - `or-as-identifier` `or` as an identifier has always been weird since the `or` (almost-)keyword has been introduced. We are deprecating the backcompat hacks from the early days of Nix in favor of making `or` a full and proper keyword.
  - `tokens-no-whitespace` Function applications without space around the arguments like `0a`, `0.00.0` or `foo"1"2` are now forbidden. The same applies to list elements. The primary reason for this deprecation is to remove foot guns around surprising tokenization rules regarding number literals, but this will also free up some syntax for other purposes (e.g. `r""` strings) for reuse at some point in the future.
  - `shadow-internal-symbols` has been expanded to also forbid shadowing `null`, `true` and `false`.
  - `ancient-let` deprecation has been turned into a full parser error instead of a warning.
  - `rec-set-overrides` deprecation has been turned into a full parser error instead of a warning.

  Many thanks to [piegames](https://git.lix.systems/piegames), [rootile (Rutile)](https://git.lix.systems/rootile), and [eldritch horrors](https://git.lix.systems/pennae) for this.

- Move `/root/.cache/nix` to `/var/cache/nix` by default [lix#634](https://git.lix.systems/lix-project/lix/issues/634) [cl/4671](https://gerrit.lix.systems/c/lix/+/4671)

  By default, Lix attempts to locate a cache directory for its operations (such
  as the narinfo cache) by checking the value of `$XDG_CACHE_DIR`.

  However, since the Nix daemon is a system service, using `$XDG_CACHE_DIR` is
  not typical in this context.

  To address this, systemd provides a better solution. Specifically, when
  `CacheDirectory=` is set in the `[Service]` section of a systemd unit, it
  automatically sets the `$CACHE_DIRECTORY` environment variable and systemd will
  manage that cache directory for us.

  Now, our systemd unit includes `CacheDirectory=nix`, which sets the
  `$CACHE_DIRECTORY` and takes precedence over `$XDG_CACHE_DIR`.

  If the daemon is run under user units, systemd will automatically set
  `$XDG_CACHE_DIR`.

  If neither of these variables is set, Lix falls back to its default behavior.
  By default, Lix will try to find a cache directory for its various operations
  (e.g. narinfo cache) by looking into `$XDG_CACHE_DIR`.

  In summary, what was stored in `/root/.cache/nix` is now moved to
  `/var/cache/nix/nix`.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Remove `fetch-closure` experimental feature [lix#1010](https://git.lix.systems/lix-project/lix/issues/1010) [cl/4595](https://gerrit.lix.systems/c/lix/+/4595)

  The `fetch-closure` experimental feature has been removed.

  Outside of allowing the user to import closure from binary cache,
  `fetchClosure` also allowed you to do the following:

  * rewrite non-CA path to CA
  * reject non-CA paths at fetching time
  * reject CA paths at fetching time

  Some people are using those mechanism to prevent users from having to build any
  package and force going via the declared cache or as a way to use ancient/old
  software without paying the evaluation cost of a second nixpkgs.

  Both use cases are somewhat of an antipattern in Nix semantics. If the user
  cannot fetch a program directly via the substituter mechanism and fall back to
  local build, this is a feature *and* a misconfiguration. If the user cannot build
  certain derivations because they are too expensive, the build directives should
  pass `-j0` or similar.

  As for the second usecase, there's a different way to do it that also allows to
  have a way to reproduce the paths that are hardcoded in that file, perform
  `import (fetchurl "https://my-cache/${hashparts storepath}.drv")` rather, i.e.
  an IFD to a possibly well known name. The backend can generate them on the fly
  or once, and possess stable names.

  Finally, as for the non-CA → CA features, Lix removed ca-derivations.
  fetchClosure offers ca-derivations-like features which suffers from similar
  shortcomings albeit lessened. It only follows that we should deprecate
  and remove these capabilities.

  Many thanks to [just1602](https://git.lix.systems/just1602) for this.


## Features

- `nix store add-path` now supports references [cl/5205](https://gerrit.lix.systems/c/lix/+/5205)

  Lix supports two categories of hashes in store paths: input-addressed and output-addressed.

  Currently, in Nix language, there is no way to produce output-addressed paths with references, as fixed-output derivations forbid references.
  However, the Nix store actually *supports* references in output-addressed paths.
  This is very useful for importing build products created outside of Lix that reference dependency store paths since such build products have no associated derivation so don't make any sense to input-address.
  Previously, output-addressed paths with references could only be created by writing a custom client to the rather-baroque Nix daemon protocol; now it's available in the CLI.

  Using `nix store add-path --references-list-json REFS_LIST_FILE SOME_PATH` with a JSON list of string store paths, you can now create such paths with the Lix CLI.
  They may be consumed from Nix language using something like `builtins.storePath` or the following which also works in pure evaluation mode:

  ```nix
  # Hack from https://git.lix.systems/lix-project/lix/issues/402#issuecomment-5889
  path:
  builtins.appendContext path {
    ${path} = {
      path = true;
    };
  }
  ```

  Many thanks to [jade](https://git.lix.systems/jade) for this.

- Add `builtins.warn` for emitting warnings from Nix code [cl/2248](https://gerrit.lix.systems/c/lix/+/2248)

  Lix now has a builtin function for emitting warnings.
  Like `builtins.trace`, it takes two arguments: the message to emit, and the expression to return.
  _Unlike_ `builtins.trace`, `builtins.warn` requires the first argument — the message — to be a string.
  In the future we may extend `builtins.warn` to accept a more structured API.

  To go along with this, we also have two new config settings:
  - [`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-warn), which, when used with `--debugger`, makes `builtins.warn` also function like [`builtins.break`](@docroot@/language/builtins.md#builtins-break).
  - [`abort-on-warn`](@docroot@/command-ref/conf-file.md#conf-abort-on-warn), which aborts evaluation entirely after the warning is emitted.

  Many thanks to [Emilia Bopp](https://git.lix.systems/milibopp) and [Qyriad](https://git.lix.systems/Qyriad) for this.

- `keep-env-derivations` is now supported for nix3 CLI (`nix profile`) [lix#1095](https://git.lix.systems/lix-project/lix/issues/1095) [cl/5332](https://gerrit.lix.systems/c/lix/+/5332)

  The `keep-env-derivations` feature is now available for `nix profile`. This allows users to prevent the garbage collection of derivations used to install a profile, even when `keep-derivations = false` (set to `true` by default).

  Previously, `nix-env` supported this feature, but `nix profile` **never** did. This caused issues when garbage collection removed the associated `.drv` files, which are required, for example, by vulnerability management tools (e.g. [vulnix](https://github.com/nix-community/vulnix)) for proper operation.

  This issue has now been resolved.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Make `log-format` a setting [cl/4686](https://gerrit.lix.systems/c/lix/+/4686)

  The [`--log-format` CLI option](@docroot@/command-ref/opt-common.md#opt-log-format) can now be set in [`nix.conf`](@docroot@/command-ref/conf-file.md#conf-log-format)!
  For example, you can now persistently enable the `multiline-with-logs` log format [added in Lix 2.91](@docroot@/release-notes/rl-2.91.md) by adding the following to your `nix.conf`:

  ```conf
  log-format = multiline-with-logs
  ```

  Or the equivalent in a NixOS configuration:
  ```nix
  {
    nix.settings.log-format = "multiline-with-logs";
  }
  ```

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

- Allow remote builders to be configured using TOML [cl/4533](https://gerrit.lix.systems/c/lix/+/4533)

  Lix now supports configuring remote builders using a TOML file instead of the old, very cursed and incomprehensible format.
  This comes with not only a human-understandable file, but also with better messages and error reports on misconfiguration.

  A more detailed Documentation can be found on the [distributed-builds](@docroot@/advanced-topics/distributed-builds.md) documentation page.

  Many thanks to [rootile (Rutile)](https://git.lix.systems/rootile) and [Qyriad](https://git.lix.systems/Qyriad) for this.

- Emit warnings when encountering IFD with `warn-import-from-derivation` [nix#13279](https://github.com/NixOS/nix/pull/13279) [cl/3879](https://gerrit.lix.systems/c/lix/+/3879)

  Instead of only being able to toggle the use of [Import from
  Derivation](https://nix.dev/manual/nix/stable/language/import-from-derivation) with
  `allow-import-from-derivation`, Lix is now able to warn users whenever IFD is encountered with
  `warn-import-from-derivation`.

  Many thanks to [Seth Flynn](https://git.lix.systems/getchoo), [gustavderdrache](https://github.com/gustavderdrache), and [Eelco Dolstra](https://github.com/edolstra) for this.


## Improvements

- Collect Flakes untrusted settings into one prompt [lix#682](https://git.lix.systems/lix-project/lix/issues/682) [cl/2921](https://gerrit.lix.systems/c/lix/+/2921)

  When working with Flakes containing untrusted settings, a prompt is shown for each setting, asking whether to vet or approve it. This looks like:

  ```
  ❯ nix flake lock
  warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  The following settings require your decision:
  - allow-dirty = false
  - sandbox = false
  Do you want to allow configuration settings to be applied?
  This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all)
  ```

  In Flakes with a large number of settings to approve or reject, this process can become tedious as each option must be handled individually.

  To address this, all untrusted settings are now consolidated into a single prompt: allowing for bulk acceptance permanently or not, rejection, or detailed review. For example:

  ### Scrutiny scenario

  ```console
  ❯ nix flake lock
  warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  The following settings require your decision:
  - allow-dirty = false
  - sandbox = false
  Do you want to allow configuration settings to be applied?
  This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) n
  warning: you can set 'accept-flake-config' to 'false' to automatically reject configuration options supplied by flakes
  Do you want to allow setting 'allow-dirty = false'? (yes for now/Allow always/no for now) y
  Do you want to allow setting 'sandbox = false'? (yes for now/Allow always/no for now) n
  ```

  ### Reject everything scenario

  ```console
  ❯ nix flake lock
  warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  The following settings require your decision:
  - allow-dirty = false
  - sandbox = false
  Do you want to allow configuration settings to be applied?
  This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) N
  Rejecting all untrusted nix.conf entries
  warning: you can set 'accept-flake-config' to 'false' to automatically reject configuration options supplied by flakes
  ```

  ### Accept everything scenario

  ```console
  ❯ nix flake lock
  warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  The following settings require your decision:
  - allow-dirty = false
  - sandbox = false
  Do you want to allow configuration settings to be applied?
  This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) y
  ```

  ### Accept everything PERMANENTLY scenario

  Note that accepting everything permanently will authorize these options for any
  further operations.

  The file containing this trust information is usually located in
  `~/.local/share/nix/trusted-settings.json` and can be edited manually to revoke
  this permission until Lix provides a first-class command for this manipulation.

  ```console
  ❯ nix flake lock
  warning: ignoring untrusted flake configuration setting 'allow-dirty', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  warning: ignoring untrusted flake configuration setting 'sandbox', pass '--accept-flake-config' to trust it (may allow the flake to gain root, see the nix.conf manual page)
  The following settings require your decision:
  - allow-dirty = false
  - sandbox = false
  Do you want to allow configuration settings to be applied?
  This may allow the flake to gain root, see the nix.conf manual page (yes for now/Allow always/no/No to all) A
  ```

  Many thanks to [isabelroses](https://git.lix.systems/isabelroses), [Raito Bezarius](https://git.lix.systems/raito), and [eldritch horrors](https://git.lix.systems/pennae) for this.

- `--check` or `--rebuild` is clearer about a missing path [lix#485](https://git.lix.systems/lix-project/lix/issues/485)

  Previously, when running Lix with --check or --rebuild, failures often surfaced
  as an unhelpful error:

  > "some outputs of '...' are not valid, so checking is not possible"

  This message could mean two different things:

  - The requested output paths don't exist at all, or,
  - Some outputs exist but are not known to Lix

  Lix cannot reliably distinguish these cases, so it treated them the same.

  We've updated the error messages to clarify what Lix can determine: whether any
  valid outputs (> 0) are present or whether no outputs are available.

  When no valid outputs can be found, Lix will now suggest building the derivation
  normally (without --check or --rebuild) before trying again.

  When some valid outputs are present, Lix now reports which ones are valid,
  shows the full list of known outputs, and also suggests building the derivation
  normally.

  In the future, Lix may automate this recovery step when it knows how to rebuild
  the paths, but implementing that safely requires more extensive changes to the
  codebase.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- `nix develop` no longer ignores the env variable `SSL_CERT_FILE` [cl/5042](https://gerrit.lix.systems/c/lix/+/5042)

  Running `nix develop` and `nix print-dev-env` on shells that define the environment variable `SSL_CERT_FILE` now works correctly by exporting that variable inside the built shell.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- Linux sandbox launch overhead greatly reduced [cl/5030](https://gerrit.lix.systems/c/lix/+/5030) [cl/5073](https://gerrit.lix.systems/c/lix/+/5073) [cl/5074](https://gerrit.lix.systems/c/lix/+/5074)

  Sandboxed builds are now much cheaper to launch on Linux, with constant management
  overhead. This will mostly be noticeable when building derivation trees containing
  many small derivations like nixpkgs' `writeFile` or `runCommand` with scripts that
  exit quickly. In synthetic tests we have seen build times of 3000 small runCommand
  drop from 80 seconds to 14 seconds, which is the most optimistic case in practice.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- mTLS store connections via a plugin [cl/3754](https://gerrit.lix.systems/c/lix/+/3754) [cl/3696](https://gerrit.lix.systems/c/lix/+/3696) [cl/3697](https://gerrit.lix.systems/c/lix/+/3697) [cl/3698](https://gerrit.lix.systems/c/lix/+/3698)

  To support use cases requiring mutual TLS (mTLS) authentication when connecting
  to remote Nix stores, e.g. private stores, we have introduced a **contributed**
  mTLS plugin extending the Lix store interface.

  This design follows an extensibility model which was brought up [by a proposal
  of making Kerberos authentication possible in Lix
  directly](https://gerrit.lix.systems/c/lix/+/3637).

  This mTLS plugin serves as a concrete example of how store connection
  mechanisms can be modularized through external plugins, without extending Lix
  core. This idea can be generalized to integrate automatic certificate renewal
  or advanced integrations with secrets engine or posture checks.

  It enables custom TLS client certificates to be used for authenticating against
  a remote store that enforces mTLS.

  To use the plugin, configure Lix manually by setting in your `nix.conf`:

  ```
  plugin-files = /a/path/to/libplugin_mtls_store.so
  ```

  Currently, this must be done explicitly. In the future, Nixpkgs will provide a
  mechanism to reference an up-to-date and curated set of plugins automatically.

  Making plugins easily consumable outside of Nixpkgs (e.g., from external plugin
  registries or binary distributions) remains an open question and will require
  further design.

  Contributed plugins come with significantly reduced **stability** and
  **maintenance** guarantees compared to the Lix core. We encourage users who
  depend on a given plugin to take on maintenance responsibilities and apply for
  ownership within the Lix mono-repository. These plugins are subject to removal
  at any time.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito), [eldritch horrors](https://git.lix.systems/pennae), [mic92](https://github.com/mic92), [vlaci](https://github.com/vlaci), and [nkk0](https://github.com/nkk0) for this.

- Add an indication of nix-shell nesting depth [lix#826](https://git.lix.systems/lix-project/lix/issues/826) [cl/4657](https://gerrit.lix.systems/c/lix/+/4657)

  When in a nix shell (either via a `nix-shell` or a `nix develop` invocation), a variable `NIX_SHELL_LEVEL` is exported to indicate the nesting depth of nix shells.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- `nix store delete` can now unlink a GC root before deleting its closure [cl/4660](https://gerrit.lix.systems/c/lix/+/4660)

  Ever build something, and then you want to delete it and whatever dependencies it downloaded?
  Before you had to resolve the `result` symlink and copy it, then delete it, *then* `nix store delete --delete-closure --skip-live` on the path you copied.
  Now you can just pass `--unlink` and the `result` symlink itself.

  Many thanks to [Qyriad](https://git.lix.systems/Qyriad) for this.

- `nix path-info` no longer lies to the user about fetching paths [lix#323](https://git.lix.systems/lix-project/lix/issues/323) [cl/4866](https://gerrit.lix.systems/c/lix/+/4866)

  When running `nix path-info` with an installable that is not present in the store, Lix no longer
  tells the user which paths are missing and that they will be fetched, as the documentation clearly
  states that this command does not fetch missing paths.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- Derivations can now be printed in detail in `nix repl` [cl/3842](https://gerrit.lix.systems/c/lix/+/3842)

  Traditionally derivations printed in the REPL would only print a formatted object
  representing the path of the derivation file it refers to. This makes inspecting
  the enhanced derivation attribute sets encountered from `mkDerivation` or similar
  wrappers more difficult. Even the `:p`/`:print` command would not elaborate attribute sets
  tagged as a derivation.

  With this change you can now use `:p`/`:print` to directly inspect a derivation
  by providing one as the top-level object. Derivation attribute sets will only be
  printed two levels deep and internal derivation attrsets will remain in unexpanded
  path form as before. `drvAttrs` will also be elided as these attributes are already
  present in the top-level attribute set of the derivation. These heuristics provide
  a balance between readability and functionality. When the `:p`/`:print` is omitted,
  a bare derivation is printed in the path format as before.

  Many thanks to [Lunaphied](https://git.lix.systems/Lunaphied) for this.

- Reject `__json` in structured attributes derivations [lix#380](https://git.lix.systems/lix-project/lix/issues/380) [cl/5286](https://gerrit.lix.systems/c/lix/+/5286)

  In structured attributes derivations, `__json` is used internally to store the
  JSON representation of the `env` attribute field that users can set.

  Unfortunately, a user can set `__json` *and* enable structured attributes,
  resulting in a broken derivation from a semantic point of view.

  As no user can benefit from setting `__json` *and* enable structured attributes,
  we disallow that possibility and throw an error from now on.

  This is not seen as a breaking change because there's no user code that can
  benefit from this behavior, hence, it's an improvement to user experience.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Shells support `$NIX_LOG_FD` now [lix#336](https://git.lix.systems/lix-project/lix/issues/336) [cl/4694](https://gerrit.lix.systems/c/lix/+/4694) [cl/4695](https://gerrit.lix.systems/c/lix/+/4695)

  Lix's "debugging" shells (`nix3-develop` and `nix-shell`) now set the
  `$NIX_LOG_FD` environment variable.

  This means that [hook logging in
  stdenv](https://github.com/NixOS/nixpkgs/pull/310387) appears while debugging
  derivations via `nix3-develop` or `nix-shell`.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Supplementary groups are now supported for daemon authentication [lix#968](https://git.lix.systems/lix-project/lix/issues/968) [cl/5021](https://gerrit.lix.systems/c/lix/+/5021)

  macOS, FreeBSD and Linux now support receiving supplementary groups during UNIX domain authentication to a Lix daemon.

  This change is particularly beneficial for systemd units with `DynamicUser=true` that need to connect to a Lix daemon, using a `SupplementaryGroups=` allocated by systemd in the context of the process. This is desirable if you wish to harden Lix clients.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito), [Tom Hubrecht](https://git.lix.systems/tom-hubrecht), [alois31](https://git.lix.systems/alois31), and [eldritch horrors](https://git.lix.systems/pennae) for this.


## Fixes

- Nix shells' `$NIX_BUILD_TOP` are shorter [lix#1044](https://git.lix.systems/lix-project/lix/issues/1044) [cl/4663](https://gerrit.lix.systems/c/lix/+/4663)

  Following the changes in 2.94.0 to shorten build directory paths, aimed at [resolving UNIX domain socket length issues](https://gerrit.lix.systems/c/lix/+/4168/13) and [improving nix-shell](https://git.lix.systems/lix-project/lix/issues/940), we inadvertently introduced an excessively long path for the `$NIX_BUILD_TOP` environment variable used by Nix shells (their effective temporary `/build` directory).

  To fix this, we replaced the `build-top-$HASH` directory name with simply `build-top`, reducing these paths by at least 30 characters.

  We also added a test to ensure that Nix shells do not introduce more than 50 extra characters relative to their base directory (e.g., `/tmp` when `$TMPDIR` is not set).

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) for this.

- Fix resolving of symlinks in flake paths [lix#106](https://git.lix.systems/lix-project/lix/issues/106) [lix#12286](https://git.lix.systems/lix-project/lix/pulls/12286) [cl/4783](https://gerrit.lix.systems/c/lix/+/4783)

  Flake paths are now canonicalized to resolve symlinks. This ensures that when a flake is accessed via a symlink, paths are resolved relative to the target directory, not the symlink's location.

  Many thanks to [stevalkr](https://github.com/stevalkr) and [xyenon](https://git.lix.systems/xyenon) for this.

- The REPL no longer considers failed loads for `:reload` [lix#50](https://git.lix.systems/lix-project/lix/issues/50) [cl/4864](https://gerrit.lix.systems/c/lix/+/4864) [cl/4865](https://gerrit.lix.systems/c/lix/+/4865) [cl/4700](https://gerrit.lix.systems/c/lix/+/4700) [cl/4889](https://gerrit.lix.systems/c/lix/+/4889)

  The [REPL](@docroot@/command-ref/new-cli/nix3-repl.md) allows "loading" files, flakes, and expressions into the environment, with the commands `:load`/`:l`, `:load-flake`/`:lf`, and `:add`/`:a` respectively.
  The results of those stay in the environment as-is even if their sources change, until the `:reload` command is used.
  However `:reload` would re-perform *all* instances of `:l`/`:lf`/`:a`, meaning you would get things like this:

  ```nix
  nix-repl> :l /tmp/texting.nix
  error: getting status of '/tmp/texting.nix': No such file or directory
  # oops, typo.
  nix-repl> :l /tmp/testing.nix

  # Do some stuff…

  nix-repl> :reload
  error: getting status of '/tmp/texting.nix': No such file or directory
  ```

  This is pretty silly, but also *incredibly* annoying, as it would stop there and *not* reload the correct files anymore.
  This effectively meant typoing any of the load commands would make `:reload` useless for the rest of the entire `nix repl` session!

  This has been fixed, so now only *successful* loads count towards `:reload`.

  Many thanks to [Raito Bezarius](https://git.lix.systems/raito) and [Qyriad](https://git.lix.systems/Qyriad) for this.

- Consistently use commit hash as rev when locking git inputs [cl/4762](https://gerrit.lix.systems/c/lix/+/4762)

  Lix will now use commit hashes instead of tag object hashes in the `rev` field
  when fetching git inputs by tag in `flake.lock` and `builtins.fetchTree` output.
  Note that this means that Lix may change some `flake.lock` files on re-locking. Old `flake.lock` files still remain valid.

  Many thanks to [goldstein](https://git.lix.systems/goldstein) for this.


## Development

- Functional lang migration [lix#856](https://git.lix.systems/lix-project/lix/issues/856) [cl/3213](https://gerrit.lix.systems/c/lix/+/3213)

  We have done it! The functional/lang framework has now been fully migrated to functional2/lang.
  This means: no more `just clean` and `just install` mess and whatever because one removed a test.
  The lang test suite is also getting a face lift, with an improved folder structure and restructuring of many tests.

  Only the first CL of the chain is provided but there's way more changes associated to this project.

  Many thanks to [piegames](https://git.lix.systems/piegames) and [rootile (Rutile)](https://git.lix.systems/rootile) for this.


## Miscellany

- Warn instead of erroring when the final destination of a transfer changes in-flight [lix#1004](https://git.lix.systems/lix-project/lix/issues/1004) [cl/4641](https://gerrit.lix.systems/c/lix/+/4641)

  Lix will now emit a warning during downloads where the final destination changes suddently mid-transfer instead of throwing an error.
  This transfer behavior has been known to happen very rarely while fetching from some CDNs.

  Many thanks to [Tom Hubrecht](https://git.lix.systems/tom-hubrecht) for this.

- `impersonate-linux-26` setting removed [cl/5047](https://gerrit.lix.systems/c/lix/+/5047)

  Linux 3.0 was released 15 years ago. The `impersonate-linux-26` setting was added
  14 years ago with no mention of it being necessary to build anything, only saying
  that it improves determinism—which isn't accurate since impersonating Linux 2.6.x
  still allows the version string to change, and the final component of the version
  does still change with each Linux release. Since this setting should be no longer
  necessary in modern systems and workarounds for building old code exist (by using
  e.g. `setarch --uname-2.6` to wrap builds) we are removing this setting from Lix.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Default to showing build logs in the new-style (nix3) CLI [cl/4674](https://gerrit.lix.systems/c/lix/+/4674)

  Lix will now show logs by default, in addition to the progress bar, when invoked through the new-style "nix3" CLI (`nix build`, etc)

  Many thanks to [K900](https://git.lix.systems/K900) for this.

- Lix daemons are now fully socket-activated on systemd setups [lix#1030](https://git.lix.systems/lix-project/lix/issues/1030)

  When launched by systemd, Lix no longer uses a persistent daemon process and uses systemd socket
  activation instead. This is necessary to support the `cgroups` and `auto-allocate-uids` features
  and may improve observability of daemon behavior with common systemd-based monitoring solutions.

  The old behavior with a single persistent daemon is still available, but disabled by default. It
  is not possible to enable both a persistent daemon and socket activation, starting one stops the
  other automatically. Existing installations should not require any changes when they're updated.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.

- Plugin interfaces have changed (again) [lix#359](https://git.lix.systems/lix-project/lix/issues/359) [cl/4933](https://gerrit.lix.systems/c/lix/+/4933) [cl/4934](https://gerrit.lix.systems/c/lix/+/4934)

  The `RegisterPrimOp` class used to register builtins has been removed. Plugins
  must now call `PluginPrimOps::add` from their `nix_plugin_entry` with the same
  parameters previously passed to `RegisterRrimOp` to register any new builtins.

  The `GlobalConfig::Register` helper class has also been removed. Adding config
  options to the system is now done with `GlobalConfig::registerGlobalConfig`; a
  plugin can add config values by calling this function from `nix_plugin_entry`.

  Many thanks to [eldritch horrors](https://git.lix.systems/pennae) for this.
