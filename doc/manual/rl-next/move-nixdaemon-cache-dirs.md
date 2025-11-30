---
synopsis: "Move /root/.cache/nix to /var/cache/nix by default"
cls: [4671]
issues: [fj#634]
category: "Breaking Changes"
credits: [raito]
---

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
