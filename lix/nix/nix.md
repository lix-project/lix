R""(

# Custom commands

> **Warning** \
> Custom commands are part of the unstable
> [lix-custom-sub-commands experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-lix-custom-sub-commands),
> and subject to change without notice.

Lix allows users to define custom subcommands by placing executables in the system's `PATH` that follow the naming convention `lix-<command>`. When a user runs `lix <command>`, Lix will attempt to locate and execute `lix-<command>` as a separate process.

Auto-completion of custom commands is not supported yet.

## Usage

A custom Lix command must be an executable script or binary named `lix-<command>` and be accessible from the `PATH`. When the user invokes `lix <command>`, Lix will execute `lix-<command>` with the given arguments.

For example, if an executable named `lix-example` exists in the `PATH`, running:

```console
$ lix example arg1 arg2
```

will be equivalent to running:

```console
$ lix-example arg1 arg2
```

# Examples

* Create a new flake:

  ```console
  # nix flake new hello
  # cd hello
  ```

* Build the flake in the current directory:

  ```console
  # nix build
  # ./result/bin/hello
  Hello, world!
  ```

* Run the flake in the current directory:

  ```console
  # nix run
  Hello, world!
  ```

* Start a development shell for hacking on this flake:

  ```console
  # nix develop
  # unpackPhase
  # cd hello-*
  # configurePhase
  # buildPhase
  # ./hello
  Hello, world!
  # installPhase
  # ../outputs/out/bin/hello
  Hello, world!
  ```

# Description

Lix is a tool for building software, configurations and other
artifacts in a reproducible and declarative way. For more information,
see the [Lix homepage](https://lix.systems).

Lix is a fork of the original implementation [CppNix](https://github.com/nixos/nix).

# Installables

> **Warning** \
> Installables are part of the unstable
> [`nix-command` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-nix-command),
> and subject to change without notice.

Many `nix` subcommands operate on one or more *installables*.
These are command line arguments that represent something that can be realised in the Nix store.

The following types of installable are supported by most commands:

- [Flake output attribute](#flake-output-attribute) (experimental)
  - This is the default
- [Store path](#store-path)
  - This is assumed if the argument is a Nix store path or a symlink to a Nix store path
- [Fileish](#nix-file), optionally qualified by an attribute path
  - Specified with `--file`/`-f`
- [Nix expression](#nix-expression), optionally qualified by an attribute path
  - Specified with `--expr`/`-E`

For most commands, if no installable is specified, `.` is assumed.
That is, Lix will operate on the default flake output attribute of the flake in the current directory.

### Flake output attribute

> **Warning** \
> Flake output attribute installables depend on both the
> [`flakes`](@docroot@/contributing/experimental-features.md#xp-feature-flakes)
> and
> [`nix-command`](@docroot@/contributing/experimental-features.md#xp-feature-nix-command)
> experimental features, and subject to change without notice.

Example: `nixpkgs#hello`

These have the form *flakeref*[`#`*attrpath*], where *flakeref* is a
[flake reference](./nix3-flake.md#flake-references) and *attrpath* is an optional attribute path. For
more information on flakes, see [the `nix flake` manual
page](./nix3-flake.md).  Flake references are most commonly a flake
identifier in the flake registry (e.g. `nixpkgs`), or a raw path
(e.g. `/path/to/my-flake` or `.` or `../foo`), or a full URL
(e.g. `github:nixos/nixpkgs` or `path:.`)

When the flake reference is a raw path (a path without any URL
scheme), it is interpreted as a `path:` or `git+file:` url in the following
way:

- If the path is within a Git repository, then the url will be of the form
  `git+file://[GIT_REPO_ROOT]?dir=[RELATIVE_FLAKE_DIR_PATH]`
  where `GIT_REPO_ROOT` is the path to the root of the git repository,
  and `RELATIVE_FLAKE_DIR_PATH` is the path (relative to the directory
  root) of the closest parent of the given path that contains a `flake.nix` within
  the git repository.
  If no such directory exists, then Lix will error-out.

  Note that the search will only include files indexed by git. In particular, files
  which are matched by `.gitignore` or have never been `git add`-ed will not be
  available in the flake. If this is undesirable, specify `path:<directory>` explicitly;

  For example, if `/foo/bar` is a git repository with the following structure:

  ```
  .
  └── baz
      ├── blah
      │   └── file.txt
      └── flake.nix
  ```

  Then `/foo/bar/baz/blah` will resolve to `git+file:///foo/bar?dir=baz`

- If the supplied path is not a git repository, then the url will have the form
  `path:FLAKE_DIR_PATH` where `FLAKE_DIR_PATH` is the closest parent
  of the supplied path that contains a `flake.nix` file (within the same file-system).
  If no such directory exists, then Lix will error-out.

  For example, if `/foo/bar/flake.nix` exists, then `/foo/bar/baz/` will resolve to
 `path:/foo/bar`

If *attrpath* is omitted, Lix tries some default values; for most
subcommands, the default is `packages.`*system*`.default`
(e.g. `packages.x86_64-linux.default`), but some subcommands have
other defaults. If *attrpath* *is* specified, *attrpath* is
interpreted as relative to one or more prefixes; for most
subcommands, these are `packages.`*system*,
`legacyPackages.*system*` and the empty prefix. Thus, on
`x86_64-linux` `nix build nixpkgs#hello` will try to build the
attributes `packages.x86_64-linux.hello`,
`legacyPackages.x86_64-linux.hello` and `hello`.

### Store path

Example: `/nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10`

These are paths inside the Nix store, or symlinks that resolve to a path in the Nix store.

A [store derivation] is also addressed by store path.

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv`

If you want to refer to an output path of that store derivation, add the output name preceded by a caret (`^`).

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv^out`

All outputs can be referred to at once with the special syntax `^*`.

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv^*`

### Fileish {#nix-file}

Example: `--file /path/to/nixpkgs hello`

When the option `-f` / `--file` *fileish* \[*attrpath*...\] is given, installables are interpreted as the value of the Nix file specified by *fileish*.
If attribute paths are provided, commands will operate on the corresponding values accessible at these paths.
The Nix expression in that file, or any selected attribute, must evaluate to a derivation.

The *fileish* itself may take one of a few different forms, the first being a simple filesystem path, e.g. `nix build -f /tmp/some-file.nix`.
Like the [import builtin](../../language/builtins.md#builtins-import), specifying a directory is equivalent to specify `default.nix` within that directory.
It may also be a [search path](../env-common.md#env-NIX_PATH) (also known as a lookup path), like `<nixpkgs>`.
Unlike using `<nixpkgs>` in a `--expr` argument, this does not require `--impure`.

To emulate the `nix-build '<nixpkgs>' -A hello` pattern, use:

```console
$ nix build -f '<nixpkgs>' hello
```

If a *fileish* starts with `http://` or `https://`, it is interpreted as the URL of a tarball which will be fetched and unpacked.
Lix will then `import` the unpacked directory, so these tarballs must include at least a single top-level directory with a file called `default.nix`.
For example, you could build from a specific version of Nixpkgs with something like:

```console
$ nix build -f "https://github.com/NixOS/nixpkgs/archive/refs/heads/release-24.11.tar.gz" firefox
```

If a *fileish* starts with `flake:`, the rest of the argument is interpreted as a [flakeref](./nix3-flake.md#flake-references) (see `nix flake --help` or `man nix3-flake`), which requires the "flakes" experimental feature to be enabled.
This is is *not quite* the same as specifying a [flake output attrpath](#flake-output-attribute).
It does *not* access the flake directly and does not even consider the existence of flake.nix, but instead fetches it as if it is not a flake at all and `import`s the unpacked directory.
In other words, it assumes that the flake has a `default.nix` file, and then interprets the attribute path relative to what `default.nix` evaluates to.

For many flakes — including Nixpkgs — this will end up evaluating to the same thing.
These two commands build the same derivation, but one from the flake, and the other from `default.nix`:

```console
$ nix build 'nixpkgs#firefox' # from flake.nix
$ nix build -f flake:nixpkgs firefox # from default.nix in the flake directory; ignores flake.nix altogether
```

Finally, for legacy reasons, if a *fileish* starts with `channel:`, the rest of the argument is interpreted as the name of a channel to fetch from `https://nixos.org/channels/$CHANNEL_NAME/nixexprs.tar.gz`.
This is a **hard coded URL** pattern and is *not* related to the subscribed channels managed by the [nix-channel](../nix-channel.md) command.

<!-- XXX(jade): it is my understanding that although a lot of content here is duplicated from nix-build.md, THESE ARE NOT THE SAME BAD PARSER AAAAAAAAAAAA -->

> **Note**: any of the special syntaxes may always be disambiguated by prefixing the path.
> For example: a file in the current directory literally called `<nixpkgs>` can be addressed as `./<nixpkgs>`, to escape the special interpretation.

In summary, a file path argument may be one of:

{{#include ../fileish-summary.md}}

### Nix expression

Example: `--expr 'import <nixpkgs> {}' hello`

When the option `-E` / `--expr` *expression* \[*attrpath*...\] is given, installables are interpreted as the value of the of the Nix expression.
If attribute paths are provided, commands will operate on the corresponding values accessible at these paths.
The Nix expression, or any selected attribute, must evaluate to a derivation.

You may need to specify `--impure` if the expression references impure inputs (such as `<nixpkgs>`).

To emulate the `nix-build -E 'with import <nixpkgs> { }; hello' pattern use:

```console
$ nix build --impure -E 'with import <nixpkgs> { }; hello'
```

## Derivation output selection

Derivations can have multiple outputs, each corresponding to a
different store path. For instance, a package can have a `bin` output
that contains programs, and a `dev` output that provides development
artifacts like C/C++ header files. The outputs on which `nix` commands
operate are determined as follows:

* You can explicitly specify the desired outputs using the syntax *installable*`^`*output1*`,`*...*`,`*outputN* — that is, a caret followed immediately by a comma-separated list of derivation outputs to select.
  For installables specified as [Flake output attributes](#flake-output-attribute) or [Store paths](#store-path), the output is specified in the same argument:

  For example, you can obtain the `dev` and `static` outputs of the `glibc` package:

  ```console
  # nix build 'nixpkgs#glibc^dev,static'
  # ls ./result-dev/include/ ./result-static/lib/
  …
  ```

  and likewise, using a store path to a "drv" file to specify the derivation:

  ```console
  # nix build '/nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^dev,static'
  …
  ```

  For `-e`/`--expr` and `-f`/`--file`, the derivation output is specified as part of the attribute path:

  ```console
  $ nix build -f '<nixpkgs>' 'glibc^dev,static'
  $ nix build --impure -E 'import <nixpkgs> { }' 'glibc^dev,static'
  ```

  This syntax is the same even if the actual attribute path is empty:

  ```console
  $ nix build -E 'let pkgs = import <nixpkgs> { }; in pkgs.glibc' '^dev,static'
  ```

* You can also specify that *all* outputs should be used using the
  syntax *installable*`^*`. For example, the following shows the size
  of all outputs of the `glibc` package in the binary cache:

  ```console
  # nix path-info --closure-size --eval-store auto --store https://cache.nixos.org 'nixpkgs#glibc^*'
  /nix/store/g02b1lpbddhymmcjb923kf0l7s9nww58-glibc-2.33-123                 33208200
  /nix/store/851dp95qqiisjifi639r0zzg5l465ny4-glibc-2.33-123-bin             36142896
  /nix/store/kdgs3q6r7xdff1p7a9hnjr43xw2404z7-glibc-2.33-123-debug          155787312
  /nix/store/n4xa8h6pbmqmwnq0mmsz08l38abb06zc-glibc-2.33-123-static          42488328
  /nix/store/q6580lr01jpcsqs4r5arlh4ki2c1m9rv-glibc-2.33-123-dev             44200560
  ```

  and likewise, using a store path to a "drv" file to specify the derivation:

  ```console
  # nix path-info --closure-size '/nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^*'
  …
  ```
* If you didn't specify the desired outputs, but the derivation has an
  attribute `meta.outputsToInstall`, Lix will use those outputs. For
  example, since the package `nixpkgs#libxml2` has this attribute:

  ```console
  # nix eval 'nixpkgs#libxml2.meta.outputsToInstall'
  [ "bin" "man" ]
  ```

  a command like `nix shell nixpkgs#libxml2` will provide only those
  two outputs by default.

  Note that a [store derivation][] (given by its `.drv` file store path) doesn't have
  any attributes like `meta`, and thus this case doesn't apply to it.

  [store derivation]: ../../glossary.md#gloss-store-derivation

* Otherwise, Lix will use all outputs of the derivation.

# Nix stores

Most `nix` subcommands operate on a *Nix store*. These are documented
in [`nix help-stores`](./nix3-help-stores.md).

)""
