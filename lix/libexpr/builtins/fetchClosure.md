---
name: fetchClosure
args: [args]
experimentalFeature: fetch-closure
---
Fetch a store path [closure](@docroot@/glossary.md#gloss-closure) from a binary cache, and return the store path as a string with context.

This function can be invoked in three ways, that we will discuss in order of preference.

**Fetch a content-addressed store path**

Example:

```nix
builtins.fetchClosure {
  fromStore = "https://cache.nixos.org";
  fromPath = /nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1;
}
```

This is the simplest invocation, and it does not require the user of the expression to configure [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys) to ensure their authenticity.

If your store path is [input addressed](@docroot@/glossary.md#gloss-input-addressed-store-object) instead of content addressed, consider the other two invocations.

**Fetch any store path and rewrite it to a fully content-addressed store path**

Example:

```nix
builtins.fetchClosure {
  fromStore = "https://cache.nixos.org";
  fromPath = /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1;
  toPath = /nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1;
}
```

This example fetches `/nix/store/r2jd...` from the specified binary cache,
and rewrites it into the content-addressed store path
`/nix/store/ldbh...`.

Like the previous example, no extra configuration or privileges are required.

To find out the correct value for `toPath` given a `fromPath`,
use [`nix store make-content-addressed`](@docroot@/command-ref/new-cli/nix3-store-make-content-addressed.md):

```console
# nix store make-content-addressed --from https://cache.nixos.org /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1
rewrote '/nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1' to '/nix/store/ldbhlwhh39wha58rm61bkiiwm6j7211j-git-2.33.1'
```

Alternatively, set `toPath = ""` and find the correct `toPath` in the error message.

**Fetch an input-addressed store path as is**

Example:

```nix
builtins.fetchClosure {
  fromStore = "https://cache.nixos.org";
  fromPath = /nix/store/r2jd6ygnmirm2g803mksqqjm4y39yi6i-git-2.33.1;
  inputAddressed = true;
}
```

It is possible to fetch an [input-addressed store path](@docroot@/glossary.md#gloss-input-addressed-store-object) and return it as is.
However, this is the least preferred way of invoking `fetchClosure`, because it requires that the input-addressed paths are trusted by the Lix configuration.

**`builtins.storePath`**

`fetchClosure` is similar to [`builtins.storePath`](#builtins-storePath) in that it allows you to use a previously built store path in a Nix expression.
However, `fetchClosure` is more reproducible because it specifies a binary cache from which the path can be fetched.
Also, using content-addressed store paths does not require users to configure [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys) to ensure their authenticity.
