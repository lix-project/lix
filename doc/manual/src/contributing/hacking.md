# Hacking

This section provides some notes on how to hack on Lix. To get the latest version of Lix from Forgejo:

```console
$ git clone https://git.lix.systems/lix-project/lix
$ cd lix
```

The following instructions assume you already have some version of Nix or Lix installed locally, so that you can use it to set up the development environment. If you don't have it installed, follow the [installation instructions].

[installation instructions]: ../installation/installation.md

## Building Lix in a development shell

### Setting up the development shell

If you are using Lix or Nix with the [`flakes`] and [`nix-command`] experimental features enabled, the following command will build all dependencies and start a shell in which all environment variables are setup for those dependencies to be found:

```bash
$ nix develop
```

That will use the default stdenv for your system. To get a shell with one of the other [supported compilation environments](#compilation-environments), specify its attribute name after a hash (which you may need to quote, depending on your shell):

```bash
$ nix develop ".#native-clangStdenvPackages"
```

For classic Nix, use:

```bash
$ nix-shell -A native-clangStdenvPackages
```

[`flakes`]: @docroot@/contributing/experimental-features.md#xp-feature-flakes
[`nix-command`]: @docroot@/contributing/experimental-features.md#xp-nix-command


### Building from the development shell

You can build and test Lix with just:

```bash
$ just setup
$ just build
$ just test --suite=check
$ just install
$ just test --suite=installcheck
```

(Check and installcheck may both be done after install, allowing you to omit the --suite argument entirely, but this is the order package.nix runs them in.)

You can also build Lix manually:

```bash
$ meson setup ./build "--prefix=$out" $mesonFlags
```

(A simple `meson setup ./build` will also build, but will do a different thing, not having the settings from package.nix applied).

```bash
$ meson compile -C build
$ meson test -C build --suite=check
$ meson install -C build
$ meson test -C build --suite=installcheck
```

In both cases, Lix will be installed to `$PWD/outputs`, the `/bin` of which is prepended to PATH in the development shells.

If the tests fail and Meson helpfully has no output for why, use the `--print-error-logs` option to `meson test`.

If you change a setting in the buildsystem (i.e., any of the `meson.build` files), most cases will automatically regenerate the Meson configuration just before compiling.
Some cases, however, like trying to build a specific target whose name is new to the buildsystem (e.g. `meson compile -C build src/libmelt/libmelt.dylib`, when `libmelt.dylib` did not exist as a target the last time the buildsystem was generated), then you can reconfigure using new settings but existing options, and only recompiling stuff affected by the changes:

```bash
$ meson setup --reconfigure build
```

Note that changes to the default values in `meson.options` or in the `default_options :` argument to `project()` are **not** propagated with `--reconfigure`.

If you want a totally clean build, you can use:

```bash
$ meson setup --wipe build
```

That will work regardless of if `./build` exists or not.

Specific, named targets may be addressed in `meson build -C build <target>`, with the "target ID", if there is one, which is the first string argument passed to target functions that have one, and unrelated to the variable name, e.g.:

```meson
libexpr_dylib = library('nixexpr', …)
```

can be addressed with:

```bash
$ meson compile -C build nixexpr
```

All targets may be addressed as their output, relative to the build directory, e.g.:

```bash
$ meson compile -C build src/libexpr/liblixexpr.so
```

But Meson does not consider intermediate files like object files targets.
To build a specific object file, use Ninja directly and specify the output file relative to the build directory:

```bash
$ ninja -C build src/libexpr/liblixexpr.so.p/nixexpr.cc.o
```

To inspect the canonical source of truth on what the state of the buildsystem configuration is, use:

```bash
$ meson introspect
```

## Building Lix outside of development shells

To build a release version of Lix for the current operating system and CPU architecture:

```console
$ nix build
```

You can also build Lix for one of the [supported platforms](#platforms).

> **Note**
>
> You can use `native-ccacheStdenvPackages` to drastically improve rebuild time.
> By default, [ccache](https://ccache.dev) keeps artifacts in `~/.cache/ccache/`.

## Platforms

Lix can be built for various platforms, as specified in [`flake.nix`]:

[`flake.nix`]: https://git.lix.systems/lix-project/lix/src/branch/main/flake.nix

- `x86_64-linux`
- `x86_64-darwin`
- `i686-linux`
- `aarch64-linux`
- `aarch64-darwin`
- `armv6l-linux`
- `armv7l-linux`

In order to build Lix for a different platform than the one you're currently
on, you need a way for your current Nix installation to build code for that
platform. Common solutions include [remote builders] and [binary format emulation]
(only supported on NixOS).

[remote builders]: ../advanced-topics/distributed-builds.md
[binary format emulation]: https://nixos.org/manual/nixos/stable/options.html#opt-boot.binfmt.emulatedSystems

Given such a setup, executing the build only requires selecting the respective attribute.
For example, to compile for `aarch64-linux`:

```console
$ nix-build --attr packages.aarch64-linux.default
```

or for Nix with the [`flakes`] and [`nix-command`] experimental features enabled:

```console
$ nix build .#packages.aarch64-linux.default
```

Cross-compiled builds are available for ARMv6 (`armv6l-linux`) and ARMv7 (`armv7l-linux`).
Add more [system types](#system-type) to `crossSystems` in `flake.nix` to bootstrap Nix on unsupported platforms.

### Building for multiple platforms at once

It is useful to perform multiple cross and native builds on the same source tree, for example to ensure that better support for one platform doesn't break the build for another.
As Lix now uses Meson, out-of-tree builds are supported first class. In the invocation

```bash
$ meson setup build
```

the argument after `setup` specifies the directory for this build, conventionally simply called "build", but it may be called anything, and you may run `meson setup <somedir>` for as many different directories as you want.
To compile the configuration for a given build directory, pass that build directory to the `-C` argument of `meson compile`:

```bash
$ meson setup some-custom-build
$ meson compile -C some-custom-build
```

## System type

Lix uses a string with the following format to identify the *system type* or *platform* it runs on:

```
<cpu>-<os>[-<abi>]
```

It is set when Lix is compiled for the given system, and determined by [Meson's `host_machine.cpu_family()` and `host_machine.system()` values](https://mesonbuild.com/Reference-manual_builtin_host_machine.html).

For historic reasons and backward-compatibility, some CPU and OS identifiers are translated from the GNU Autotools naming convention in [`meson.build`](https://git.lix.systems/lix-project/lix/blob/main/meson.build) as follows:

| `host_machine.cpu_family()`             | Nix                 |
|----------------------------|---------------------|
| `x86`                      | `i686`              |
| `i686`                     | `i686`              |
| `i686`                     | `i686`              |
| `arm6`                     | `arm6l`             |
| `arm7`                     | `arm7l`             |
| `linux-gnu*`               | `linux`             |
| `linux-musl*`              | `linux`             |

## Compilation environments

Lix can be compiled using multiple environments:

- `stdenv`: default;
- `gccStdenv`: force the use of `gcc` compiler;
- `clangStdenv`: force the use of `clang` compiler;
- `ccacheStdenv`: enable [ccache], a compiler cache to speed up compilation.

To build with one of those environments, you can use

```console
$ nix build .#nix-ccacheStdenv
```

for flake-enabled Nix, or

```console
$ nix-build --attr nix-ccacheStdenv
```

for classic Nix.

You can use any of the other supported environments in place of `nix-ccacheStdenv`.

## Editor integration

The `clangd` LSP server is installed by default in each development shell.
See [supported compilation environments](#compilation-environments) and instructions how to set up a shell [with flakes](#nix-with-flakes) or in [classic Nix](#classic-nix).

Clangd requires a compilation database, which Meson generates by default. After running `meson setup`, there will already be a `compile_commands.json` file in the build directory.
Some editor configurations may prefer that file to be in the root directory, which you can accomplish with a simple:

```bash
$ ln -sf ./build/compile_commands.json ./compile_commands.json
```

Configure your editor to use the `clangd` from the shell, either by running it inside the development shell, or by using [nix-direnv](https://github.com/nix-community/nix-direnv) and [the appropriate editor plugin](https://github.com/direnv/direnv/wiki#editor-integration).

> **Note**
>
> For some editors (e.g. Visual Studio Code), you may need to install a [special extension](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd) for the editor to interact with `clangd`.
> Some other editors (e.g. Emacs, Vim) need a plugin to support LSP servers in general (e.g. [lsp-mode](https://github.com/emacs-lsp/lsp-mode) for Emacs and [vim-lsp](https://github.com/prabirshrestha/vim-lsp) for vim).
> Editor-specific setup is typically opinionated, so we will not cover it here in more detail.

### Checking links in the manual

The build checks for broken internal links.
This happens late in the process, so `nix build` is not suitable for iterating.
To build the manual incrementally, run:

```console
meson compile -C build manual
```

[`mdbook-linkcheck`] does not implement checking [URI fragments] yet.

[`mdbook-linkcheck`]: https://github.com/Michael-F-Bryan/mdbook-linkcheck
[URI fragments]: https://en.wikipedia.org/wiki/URI_fragment

#### `@docroot@` variable

`@docroot@` provides a base path for links that occur in reusable snippets or other documentation that doesn't have a base path of its own.

If a broken link occurs in a snippet that was inserted into multiple generated files in different directories, use `@docroot@` to reference the `doc/manual/src` directory.

If the `@docroot@` literal appears in an error message from the `mdbook-linkcheck` tool, the `@docroot@` replacement needs to be applied to the generated source file that mentions it.
See existing `@docroot@` logic in the [Makefile].
Regular markdown files used for the manual have a base path of their own and they can use relative paths instead of `@docroot@`.

## API documentation

Doxygen API documentation will be available online [in the future](https://git.lix.systems/lix-project/lix/issues/422).
You can also build and view it yourself:

```console
# nix build .#hydraJobs.internal-api-docs
# xdg-open ./result/share/doc/nix/internal-api/html/index.html
```

or inside a `nix develop` shell by running:

```bash
$ meson configure build -Dinternal-api-docs=enabled
$ meson compile -C build internal-api-docs
$ xdg-open ./outputs/doc/share/doc/nix/internal-api/html/index.html
```

## Coverage analysis

A coverage analysis report is [available
online](https://hydra.nixos.org/job/nix/master/coverage/latest/download-by-type/report/coverage). You
can build it yourself:

```
# nix build .#hydraJobs.coverage
# xdg-open ./result/coverage/index.html
```

Metrics about the change in line/function coverage over time are also
[available](https://hydra.nixos.org/job/nix/master/coverage#tabs-charts).

## Add a release note

`doc/manual/rl-next` contains release notes entries for all unreleased changes.

User-visible changes should come with a release note.

### Add an entry

Here's what a complete entry looks like.
The file name is not incorporated in the final document, and is generally a super brief summary of the change synopsis.

```markdown
---
synopsis: Basically a title
# 1234 or gh#1234 will refer to CppNix GitHub, fj#1234 will refer to a Lix forgejo issue.
issues: [1234, fj#1234]
# Use this *only* if there is a CppNix pull request associated with this change.
prs: 1238
# List of Lix Gerrit changelist numbers.
# If there is an associated Lix GitHub PR, just put in the Gerrit CL number.
cls: [123]
# Heading that this release note will appear under.
category: Breaking Changes
# Add a credit mention in the bottom of the release note.
# your-name is used as a key into doc/manual/change-authors.yml for metadata
credits: [your-name]
---

Here's one or more paragraphs that describe the change.

- It's markdown
- Add references to the manual using @docroot@
```

Significant changes should add the following header, which moves them to the top.

```
significance: significant
```

The following categories of release notes are supported (see `maintainers/build-release-notes.py`):
- Breaking Changes
- Features
- Improvements
- Fixes
- Packaging
- Development
- Miscellany

The `credits` field, if present, gives credit to the author of the patch in the release notes with a message like "Many thanks to (your-name) for this" and linking to GitHub or Forgejo profiles if listed.

If you are forward-porting a change from CppNix, please credit the original author, and optionally credit yourself.
When adding credits metadata for people external to the project and deciding whether to put in a `display_name`, consider what they are generally known as in the community; even if you know their full name (e.g. from their GitHub profile), we suggest only adding it as a display name if that is what they go by in the community.
There are multiple reasons we follow this practice, but it boils down to privacy and consent: we would rather not capture full names that are not widely used in the community without the consent of the parties involved, even if they are publicly available.
As of this writing, the entries with full names as `display_name` are either members of the CppNix team or people who added them themselves.

The names specified in `credits` are used as keys to look up the authorship info in `doc/manual/change-authors.yml`.
The only mandatory part is that every key appearing in `credits` has an entry present in `change-authors.yml`.
All of the following properties are optional; you can specify `{}` as the metadata if you want a simple non-hyperlinked mention.
The following properties are supported:

- `display_name`: display name used in place of the key when showing names, if present.
- `forgejo`: Forgejo username. The name in the release notes will be a link to this, if present.
- `github`: GitHub username, used if `forgejo` is not set, again making a link.

### Build process

Releases have a precomputed `rl-MAJOR.MINOR.md`, and no `rl-next.md`.
Set `buildUnreleasedNotes = true;` in `flake.nix` to build the release notes on the fly.
