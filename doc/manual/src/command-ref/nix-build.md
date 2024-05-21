# Name

`nix-build` - build a Nix expression

# Synopsis

`nix-build` [*fileish…*]
  [`--arg` *name* *value*]
  [`--argstr` *name* *value*]
  [{`--attr` | `-A`} *attrPath*]
  [`--no-out-link`]
  [`--dry-run`]
  [{`--out-link` | `-o`} *outlink*]

# Disambiguation

This man page describes the command `nix-build`, which is distinct from [`nix build`](./new-cli/nix3-build.md).
For documentation on the latter, run `nix build --help` or see `man nix3-build`.

# Description

The `nix-build` command builds the derivations described by the Nix
expressions in each *fileish*. If the build succeeds, it places a symlink to
the result in the current directory. The symlink is called `result`. If
there are multiple Nix expressions, or the Nix expressions evaluate to
multiple derivations, multiple sequentially numbered symlinks are
created (`result`, `result-2`, and so on).

If no *fileish* is specified, then `nix-build` will use `default.nix` in
the current directory, if it exists.

## Fileish Syntax

A given *fileish* may take one of a few different forms, the first being a simple filesystem path, e.g. `nix-build /tmp/some-file.nix`.
Like the [import builtin](../language/builtins.md#builtins-import) specifying a directory is equivalent to specifying `default.nix` within that directory.
It may also be a [search path](./env-common.md#env-NIX_PATH) (also known as a lookup path) like `<nixpkgs>`, which is convenient to use with `--attr`/`-A`:

```console
$ nix-build '<nixpkgs>' -A firefox
```

(Note the quotation marks around `<nixpkgs>`, which will be necessary in most Unix shells.)

If a *fileish* starts with `http://` or `https://`, it is interpreted as the URL of a tarball which will be fetched and unpacked.
Lix will then `import` the unpacked directory, so these tarballs must include at least a single top-level directory with a file called `default.nix`
For example, you could build from a specific version of Nixpkgs with something like:

```console
$ nix-build "https://github.com/NixOS/nixpkgs/archive/refs/heads/release-23.11.tar.gz" -A firefox
```

If a path starts with `flake:`, the rest of the argument is interpreted as a [flakeref](./new-cli/nix3-flake.md#flake-references) (see `nix flake --help` or `man nix3-flake`), which requires the "flakes" experimental feature to be enabled.
Lix will fetch the flake, and then `import` its unpacked directory, so the flake must include a file called `default.nix`.
For example, the flake analogues to the above `nix-build` commands are:

```console
$ nix-build flake:nixpkgs -A firefox
$ nix-build flake:github:NixOS/nixpkgs/release-23.11 -A firefox
```

Finally, for legacy reasons, if a path starts with `channel:`, the rest of the argument is interpreted as the name of a *nixpkgs* channel tarball to fetch from `https://nixos.org/channels/$CHANNEL_NAME/nixexprs.tar.xz`.
This is a **hard coded URL** pattern and is *not* related to the subscribed channels managed by the [nix-channel](./nix-channel.md) command.

> **Note**: any of the special syntaxes may always be disambiguated by prefixing the path.
> For example: a file in the current directory literally called `<nixpkgs>` can be addressed as `./<nixpkgs>`, to escape the special interpretation.

In summary, a path argument may be one of:

{{#include ./fileish-summary.md}}

## Notes

`nix-build` is essentially a wrapper around
[`nix-instantiate`](nix-instantiate.md) (to translate a high-level Nix
expression to a low-level [store derivation]) and [`nix-store
--realise`](@docroot@/command-ref/nix-store/realise.md) (to build the store
derivation).

[store derivation]: ../glossary.md#gloss-store-derivation

> **Warning**
>
> The result of the build is automatically registered as a root of the
> Nix garbage collector. This root disappears automatically when the
> `result` symlink is deleted or renamed. So don’t rename the symlink.

# Options

All options not listed here are passed to
[`nix-store --realise`](nix-store/realise.md),
except for `--arg` and `--attr` / `-A` which are passed to [`nix-instantiate`](nix-instantiate.md).

  - <span id="opt-no-out-link">[`--no-out-link`](#opt-no-out-link)<span>

    Do not create a symlink to the output path. Note that as a result
    the output does not become a root of the garbage collector, and so
    might be deleted by `nix-store --gc`.

  - <span id="opt-dry-run">[`--dry-run`](#opt-dry-run)</span>

    Show what store paths would be built or downloaded.

  - <span id="opt-out-link">[`--out-link`](#opt-out-link)</span> / `-o` *outlink*

    Change the name of the symlink to the output path created from
    `result` to *outlink*.

{{#include ./status-build-failure.md}}

{{#include ./opt-common.md}}

{{#include ./env-common.md}}

# Examples

```console
$ nix-build '<nixpkgs>' --attr firefox
store derivation is /nix/store/qybprl8sz2lc...-firefox-1.5.0.7.drv
/nix/store/d18hyl92g30l...-firefox-1.5.0.7

$ ls -l result
lrwxrwxrwx  ...  result -> /nix/store/d18hyl92g30l...-firefox-1.5.0.7

$ ls ./result/bin/
firefox  firefox-config
```

If a derivation has multiple outputs, `nix-build` will build the default
(first) output. You can also build all outputs:

```console
$ nix-build '<nixpkgs>' --attr openssl.all
```

This will create a symlink for each output named `result-outputname`.
The suffix is omitted if the output name is `out`. So if `openssl` has
outputs `out`, `bin` and `man`, `nix-build` will create symlinks
`result`, `result-bin` and `result-man`. It’s also possible to build a
specific output:

```console
$ nix-build '<nixpkgs>' --attr openssl.man
```

This will create a symlink `result-man`.

Build a Nix expression given on the command line:

```console
$ nix-build --expr 'with import <nixpkgs> { }; runCommand "foo" { } "echo bar > $out"'
$ cat ./result
bar
```

Build the GNU Hello package from the latest revision of the master
branch of Nixpkgs:

```console
$ nix-build https://github.com/NixOS/nixpkgs/archive/master.tar.gz --attr hello
```
