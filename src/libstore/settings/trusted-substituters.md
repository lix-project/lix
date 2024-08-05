---
name: trusted-substituters
internalName: trustedSubstituters
type: StringSet
default: []
aliases: [trusted-binary-caches]
---
A list of [Nix store URLs](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format), separated by whitespace.
These are not used by default, but users of the Nix daemon can enable them by specifying [`substituters`](#conf-substituters).

Unprivileged users (those set in only [`allowed-users`](#conf-allowed-users) but not [`trusted-users`](#conf-trusted-users)) can pass as `substituters` only those URLs listed in `trusted-substituters`.
