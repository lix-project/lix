---
name: substituters
internalName: substituters
type: Strings
default: [https://cache.nixos.org/]
aliases: [binary-caches]
---
A list of [URLs of Nix stores](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format) to be used as substituters, separated by whitespace.
A substituter is an additional [store](@docroot@/glossary.md#gloss-store) from which Lix can obtain [store objects](@docroot@/glossary.md#gloss-store-object) instead of building them.

Substituters are tried based on their priority value, which each substituter can set independently.
Lower value means higher priority.
The default is `https://cache.nixos.org`, which has a priority of 40.

At least one of the following conditions must be met for Lix to use a substituter:

- The substituter is in the [`trusted-substituters`](#conf-trusted-substituters) list
- The user calling Lix is in the [`trusted-users`](#conf-trusted-users) list

In addition, each store path should be trusted as described in [`trusted-public-keys`](#conf-trusted-public-keys)
