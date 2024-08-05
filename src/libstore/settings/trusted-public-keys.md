---
name: trusted-public-keys
internalName: trustedPublicKeys
type: Strings
default: [cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=]
aliases: [binary-cache-public-keys]
---
A whitespace-separated list of public keys.

At least one of the following condition must be met
for Lix to accept copying a store object from another
Nix store (such as a substituter):

- the store object has been signed using a key in the trusted keys list
- the [`require-sigs`](#conf-require-sigs) option has been set to `false`
- the store object is [output-addressed](@docroot@/glossary.md#gloss-output-addressed-store-object)
