---
name: store
internalName: storeUri
type: std::string
defaultExpr: 'getEnv("NIX_REMOTE").value_or("auto")'
defaultText: '`auto`'
---
The [URL of the Nix store](@docroot@/command-ref/new-cli/nix3-help-stores.md#store-url-format)
to use for most operations.
See [`nix help-stores`](@docroot@/command-ref/new-cli/nix3-help-stores.md)
for supported store types and settings.
