# Multi-User Mode

To allow a Nix store to be shared safely among multiple users, it is important that users cannot meaningfully influence the execution of derivation builds such that they could inject malicious code into them without changing their (either input- or output- addressed) hash.
If they could do so, they could install a Trojan horse in some package and compromise the accounts of other users.

To prevent this, the Nix store and database are owned by some privileged user (usually `root`) and builders are executed under unprivileged system user accounts (usually named `nixbld1`, `nixbld2`, etc.).
When an unprivileged user runs a Nix command, actions that operate on the Nix store (such as builds) are forwarded to a *Nix daemon* running under the owner of the Nix store/database that performs the operation.

The buried lede in the above sentence is that *currently*, even in multi-user mode using a daemon, if executing as the user that owns the store, Lix directly manipulates the store unless `--store daemon` is specified.
[We intend to change this in the future][multi-user-should-not-be-root].

<div class="warning">
The Lix team considers the goal of the sandbox to be primarily for preventing reproducibility mistakes, and does not consider multi-user mode to be a strong security boundary between users.

Do not evaluate or build untrusted, potentially-malicious, Nix language code on machines that you care deeply about maintaining user isolation on.

Although we would consider any sandbox escapes to be serious security bugs and we intend to fix them, we are not confident enough in the daemon's security to call the daemon a security boundary.
</div>

[multi-user-should-not-be-root]: https://git.lix.systems/lix-project/lix/issues/18

## Trust model

There are two categories of users of the Lix daemon: trusted users and untrusted users.
The Lix daemon only allows connections from users that are either trusted users, or are specified in, or are members of groups specified in, [`allowed-users`](../command-ref/conf-file.md#conf-allowed-users) in `nix.conf`.
Trusted users are users and users of groups specified in [`trusted-users`](../command-ref/conf-file.md#conf-trusted-users) in `nix.conf`.

All users of the Lix daemon may do the following to bring things into the Nix store:

- Users may load derivations and output-addressed files into the store with `nix-store --add` or through Nix language code.
- Users may locally build derivations, either of the output-addressed or input-addressed variety, creating output paths.

  Note that [fixed-output derivations only consider name and hash](https://github.com/NixOS/nix/issues/969), so it is possible to write a fixed-output derivation for something important with a bogus hash and have it resolve to something else already built in the store.

  On systems with `sandbox` enabled (default on Linux; [not *yet* on macOS][sandbox-enable-macos]), derivations are either:
    - Input-addressed, so they are run in the sandbox with no network access, with the following exceptions:

      - The (poorly named, since it is not *just* about chroot) property `__noChroot` is set on the derivation and `sandbox` is set to `relaxed`.
      - On macOS, the derivation property `__darwinAllowLocalNetworking` allows network access to localhost from input-addressed derivations regardless of the `sandbox` setting value.
        This property exists with such semantics because macOS has no network namespace equivalent to isolate individual processes' localhost networking.
      - On macOS, the derivation property `__sandboxProfile` accepts extra sandbox profile S-expressions, allowing derivations to bypass arbitrary parts of the sandbox without altogether disabling it.
        This is only permitted when `sandbox` is set to `relaxed`.
    - Output-addressed, so they are run with network access but their result must match an expected hash.

  Trusted users may set any setting, including `sandbox = false`, so the sandbox state can be different at runtime from what is described in `nix.conf` for builds invoked with such settings.
- Users may copy appropriately-signed derivation outputs into the store.

  By default, any paths *copied into a store* (such as by substitution) must have signatures from [`trusted-public-keys`](../command-ref/conf-file.md#conf-trusted-public-keys) unless they are [output-addressed](../glossary.md#gloss-output-addressed-store-object).

  Unsigned paths may be copied into a store if [`require-sigs`](../command-ref/conf-file.md#conf-require-sigs) is disabled in the daemon's configuration (not default), or if the client is a trusted user and passed `--no-check-sigs` to `nix copy`.
- Users may request that the daemon substitutes appropriately-signed derivation outputs from a binary cache in the daemon's [`substituters`](../command-ref/conf-file.md#conf-substituters) list.

  Untrusted clients may also specify additional values for `substituters` (via e.g. `--extra-substituters` on a Nix command) that are listed in [`trusted-substituters`](../command-ref/conf-file.md#conf-trusted-substituters).

  A client could in principle substitute such paths itself then copy them to the daemon (see clause above) if they are appropriately signed but are *not* from a trusted substituter, however this is not implemented in the current Lix client to our knowledge, at the time of writing.
  This probably means that `trusted-substituters` is a redundant setting except insofar as such substitution would have to be done on the client rather than as root on the daemon; and it is highly defensible to not allow random usage of our HTTP client running as root.

[sandbox-enable-macos]: https://git.lix.systems/lix-project/lix/issues/386

### The Lix daemon as a security non-boundary

The Lix team and wider community does not consider the Lix daemon to be a *security boundary* against malicious Nix language code.

Although we do our best to make it secure, we do not recommend sharing a Lix daemon with potentially malicious users.
That means that public continuous integration (CI) builds of untrusted Nix code should not share builders with CI that writes into a cache used by trusted infrastructure.

For example, [hydra.nixos.org], which is the builder for [cache.nixos.org], does not execute untrusted Nix language code; a separate system, [ofborg] is used for CI of nixpkgs pull requests.
The build output of pull request CI is never pushed to [cache.nixos.org], and those systems are considered entirely untrusted.

This is because, among other things, the Lix sandbox is *more* susceptible to kernel exploits than Docker, which, unlike Lix, blocks nested user namespaces via `seccomp` in its default policy, and there have been many kernel bugs only exposed to unprivileged users via user namespaces allowing otherwise-root-only system calls.
In general, the Lix sandbox is set up to be relatively unrestricted while maintaining its goals of building useful, reproducible software; security is not its primary goal.

The Lix sandbox is a custom *non-rootless* Linux container implementation that has not been audited to nearly the same degree as Docker and similar systems.
Also, the Lix daemon is a complex and historied C++ executable running as root with very little privilege separation.
All of this means that a security hole in the Lix daemon gives immediate root access.
Systems like Docker (especially non-rootless Docker) should *themselves* probably not be used in a multi-tenant manner with mutually distrusting tenants, but the Lix daemon *especially* should not be used as such as of this writing.

The primary purpose of the sandbox is to strongly encourage packages to be reproducible, a goal which it is generally quite successful at.

[hydra.nixos.org]: https://hydra.nixos.org
[ofborg]: https://github.com/NixOS/ofborg
[cache.nixos.org]: https://cache.nixos.org

### Trusted users

Trusted users are permitted to set any setting and bypass security restrictions on the daemon.
They are currently in widespread use for a couple of reasons such as remote builds (which we [intend to fix](https://git.lix.systems/lix-project/lix/issues/171)).

Trusted users are effectively root on Nix daemons running as root (the default configuration) for *at least* the following reasons, and should be thus thought of as equivalent to passwordless sudo.
This is not a comprehensive list.

- They may copy an unsigned malicious built output into the store for `systemd` or anything else that will run as root, then when the system is upgraded, that path will be used from the local store rather than substituted.
- They may set the following settings that are commands the daemon will run as root:
  - `build-hook`
  - `diff-hook`
  - `pre-build-hook`
  - `post-build-hook`
- They may set `build-users-group`.

  In particular, they may set it to empty string, which runs builds as root with respect to the rest of the system (!!).
  We, too, [think that is absurd and intend to not accept such a configuration](https://git.lix.systems/lix-project/lix/issues/242).
  It is then simply an exercise to the reader to find a daemon that does `SCM_CREDENTIALS` over a `unix(7)` socket and lets you run commands as root, and mount it into the sandbox with `extra-sandbox-paths`.

  At the very least, the Lix daemon itself (since `root` is a trusted user by default) and probably `systemd` qualify for this.
- They may set the `builders` list, which will have ssh run as root.
  We aren't sure if there is a way to abuse this for command execution but it's plausible.

Note that setting `accept-flake-config` allows arbitrary Nix flakes to set Nix settings in the `nixConfig` stanza.
Do not set this setting or pass `--accept-flake-config` while executing untrusted Nix language code as a trusted user for the reasons above!

## Build users

The *build users* are the special UIDs under which builds are performed.
A build user is selected for a build by looking in the group specified by [`build-users-group`](../command-ref/conf-file.md#conf-build-users-group), by default, `nixbld`, then a member of that group not currently executing a build is selected for the build.
The build users should not be members of any other group.

There can never be more concurrent builds than the number of build users, unless using [`auto-allocate-uids`](../command-ref/conf-file.md#conf-auto-allocate-uids) ([tracking issue][auto-allocate-uids-issue]).

[auto-allocate-uids-issue]: https://git.lix.systems/lix-project/lix/issues/387

If, for some reason, you need to create such users manually, the following command will create 10 build users on Linux:

```console
$ groupadd -r nixbld
$ for n in $(seq 1 10); do useradd -c "Nix build user $n" \
    -d /var/empty -g nixbld -G nixbld -M -N -r -s "$(which nologin)" \
    nixbld$n; done
```

## Running the daemon

The [Nix daemon](../command-ref/nix-daemon.md) can be started manually as follows (as `root`):

```console
# nix-daemon
```

In standard installations of Lix, the daemon is started by a `systemd` unit (Linux) or `launchd` service (macOS).
