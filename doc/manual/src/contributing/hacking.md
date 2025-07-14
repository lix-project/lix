# Hacking

This section provides some notes on how to hack on Lix. To get the latest version of Lix from Forgejo:

```console
$ git clone https://git.lix.systems/lix-project/lix
$ cd lix
```

The following instructions assume you already have some version of Nix or Lix installed locally, so that you can use it to set up the development environment. If you don't have it installed, follow the [installation instructions].

[installation instructions]: ../installation/installation.md

A typical development flow for simple changes in Lix looks like:
- [Set up and build Lix](#building)
- For large changes, check in regarding design and possibly create an RFD issue on Forgejo
- Make the changes in your editor
- [Send the changes to Gerrit](#sending-to-gerrit)
- Once you have the number for the CL from Gerrit to put in the changelog, [write a changelog entry](#release-notes) and amend it into the commit
- Update the Gerrit change by submitting it with the same command as the first time
- Request and receive a code review
- Address feedback from the review
- Amend commits, send to Gerrit again
- Submit the approved change

## Building Lix in a development shell {#building}

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

Run a clean build and test with `just clean setup build install test`.

You can also run the unit tests and integration tests separately:

```bash
$ just setup build test-unit
$ just install test-integration
```

Many justfile aliases have a `-custom` variant which pass extra arguments to `meson`.
For example, to work on both Lix and nix-eval-jobs you can run:

```
$ just setup-custom -Dnix-eval-jobs=enabled
$ # or
$ mesonFlags=-Dnix-eval-jobs=enabled just setup
```

Note that only targets which don't accept extra arguments can be used when
running multiple targets at once; `just setup build` is fine, but `just
setup-custom build` is an error. The `test` target is usually the last one to
run, so it always accepts extra arguments.

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
$ meson compile -C build lix/libexpr/liblixexpr.so
```

But Meson does not consider intermediate files like object files targets.
To build a specific object file, use Ninja directly and specify the output file relative to the build directory:

```bash
$ ninja -C build lix/libexpr/liblixexpr.so.p/nixexpr.cc.o
```

To inspect the canonical source of truth on what the state of the buildsystem configuration is, use:

```bash
$ meson introspect
```

## Sending changes to Gerrit for review {#sending-to-gerrit}

We use Gerrit for all our code review in Lix.
Our instance is at <https://gerrit.lix.systems>.

There's much more information about how to use Gerrit in the [wiki section on Gerrit][wiki-gerrit] including how to use Jujutsu, how to use the UI and more.
The Snix project also has some Gerrit information [in their contributing docs][snix-gerrit].

[wiki-gerrit]: https://wiki.lix.systems/books/lix-contributors/chapter/gerrit
[snix-gerrit]: https://snix.dev/docs/guides/contributing/

The gist is that once you have your SSH key and git remote set up, you can send commits for review with:

```
$ git remote set-url origin ssh://YOURUSERNAME@gerrit.lix.systems:2022/lix
$ git push origin HEAD:refs/for/main
```

Then, you can request a review via the "Reply" button on the web UI.
If you click "Suggest Owners", it will try to suggest the maintainers of the area of the code change to send review requests to.
Requesting reviews from multiple people is normal.

We do our best to respond to directly sent reviews in a few days, so feel free to request another reviewer or ask on Matrix if you've not got a response for a while.
Keep in mind that Lix is a volunteer project and we have limited bandwidth, so some changes aren't feasible to shepherd through; please check in on Matrix at design time when doing large changes.

Once you get a `Code-Review+2` vote on your change, it's rebased on `main` and CI marks it `Verified+1`, you're able (and usually expected, so you can have a second chance to check it over) to hit the Submit button to merge it.
If the change appears as "Rebase Required", you need to rebase it on `main` locally or via the Gerrit UI and wait for `Verified+1` before the Submit button is made active
The `Code-Review+2` from before will stick around through trivial rebases so no need to re-request review for a mere rebase.

## Building Lix with `nix`

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
- `x86_64-freebsd`
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

### Cross compiling using the Lix flake

Lix can also be easily cross compiled to the following arbitrarily-chosen system doubles, which can be useful for bootstrapping Lix on new platforms.
These are specified in `crossSystems` in `flake.nix`; feel free to submit changes to add new ones if they are useful to you.

- `armv6l-linux`
- `armv7l-linux`
- `aarch64-linux`
- `riscv64-linux`

For example, to cross-compile Lix for `armv6l-linux` from another Linux, use the following:

```console
$ nix build .#nix-armv6l-linux
```

It's also possible to cross-compile a tarball of binaries suitable for the Lix installer, for example, for `riscv64-linux`:

```console
$ nix build .#nix-riscv64-linux.passthru.binaryTarball
```

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

For historic reasons and backward-compatibility, some CPU and OS identifiers are translated from the GNU Autotools naming convention in [`meson.build`](https://git.lix.systems/lix-project/lix/src/branch/main/meson.build) as follows:

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

for <a id="nix-with-flakes">flake-enabled Nix</a>, or

```console
$ nix-build --attr nix-ccacheStdenv
```

for <a id="classic-nix">classic Nix</a>.

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

# Manual and documentation

## Building the manual

To build the manual incrementally, run:

```console
meson compile -C build manual
```

[`mdbook-linkcheck`] does not implement checking [URI fragments] yet.

[`mdbook-linkcheck`]: https://github.com/Michael-F-Bryan/mdbook-linkcheck
[URI fragments]: https://en.wikipedia.org/wiki/URI_fragment

The built manual is in `build/doc/manual/manual/index.html`.

The build checks for broken internal links.
This happens late in the process, so `nix build` is not suitable for iterating and it's recommended to use the `meson` command above instead.

### `@\docroot\@` variable

`@\docroot\@` provides a base path for links that occur in reusable snippets or other documentation that doesn't have a base path of its own.

If a broken link occurs in a snippet that was inserted into multiple generated files in different directories, use `@\docroot\@` to reference the `doc/manual/src` directory.

If the `@\docroot\@` literal appears in an error message from the `mdbook-linkcheck` tool, the `@\docroot\@` replacement needs to be applied to the generated source file that mentions it.
See existing `@\docroot\@` logic in `doc/manual/substitute.py`.
Regular markdown files used for the manual have a base path of their own and they can use relative paths instead of `@\docroot\@`.

## API documentation

Doxygen API documentation will be available online in the future ([tracking issue](https://git.lix.systems/lix-project/lix/issues/422)).
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

A coverage analysis report will be available online in the future (FIXME(lix-hydra)).
You can build it yourself:

```
# nix build .#hydraJobs.coverage
# xdg-open ./result/coverage/index.html
```

Metrics about the change in line/function coverage over time will be available in the future (FIXME(lix-hydra)).

## Add a release note {#release-notes}

`doc/manual/rl-next` contains release notes entries for all unreleased changes.

User-visible changes should come with a release note.
Developer-facing changes should have a release note in the Development category if they are significant and if developers should know about them.

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
Development releases have a generated `rl-next.md`.

# Adding experimental or deprecated features, global settings, or builtins

Experimental and deprecated features, global settings, and builtins are generally referenced both in the code and in the documentation.
To prevent duplication or divergence, they are defined in data files, and a script generates the necessary glue.
The data file format is similar to the release notes: it consists of a YAML metadata header, followed by the documentation in Markdown format.

## Experimental or deprecated features

Experimental and deprecated features support the following metadata properties:
* `name` (required): user-facing name of the feature, to be used in `nix.conf` options and on the command line.
  This should also be the stem of the file name (with extension `md`).
* `internalName` (required): identifier used to refer to the feature inside the C++ code.

Experimental feature data files should live in `lix/libutil/experimental-features`, and deprecated features in `lix/libutil/deprecated-features`.
They must be listed in the `experimental_feature_definitions` or `deprecated_feature_definitions` lists in `lix/libutil/meson.build` respectively to be considered by the build system.

## Global settings

Global settings support the following metadata properties:
* `name` (required): user-facing name of the setting, to be used as key in `nix.conf` and in the `--option` command line argument.
* `internalName` (required): identifier used to refer to the setting inside the C++ code.
* `platforms` (optional): a list specifying the platforms on which this setting is available.
  If not specified, it is available on all platforms.
  Valid platform names are `darwin`, `linux`.
* `type` (optional): C++ type of the setting value.
  This specifies the setting object type as `Setting<T>`; if more control is required, use `settingType` instead.
* `settingType` (required if `type` is not specified): C++ type of the setting object.
* `default` (optional): default value of the setting.
  `null`, truth values, integers, strings and lists are supported as long as the correct YAML type is used, `type` is not taken into account).
  Other types, machine-dependent values or non-standard representations must be handled using `defaultExpr` and `defaultText` instead.
* `defaultExpr` (required if `default` is not specified): a string containing the C++ expression representing the default value.
* `defaultText` (required if `default` is not specified): a string containing the Markdown expression representing the default value in the documentation.
  Literal values are conventionally surrounded by backticks, and a system-dependent value is signaled by `*machine-specific*`.
* `aliases` (optional): a list of secondary user-facing names under which the setting is available.
  Defaults to empty if not specified.
* `experimentalFeature` (optional): the user-facing name of the experimental feature which needs to be enabled to change the setting.
  If not specified, no experimental feature is required.
* `deprecated` (optional): whether the setting is deprecated and shown as such in the documentation for `nix.conf`.
  Defaults to false if not specified.

Settings are not collected in a single place in the source tree, so an appropriate place needs to be found for the setting to live.
Look for related setting definition files under second-level subdirectories of `lix` whose name includes `settings`.
Then add the new file there, and don't forget to register it in the appropriate `meson.build` file.

## Builtin functions

The following metadata properties are supported for builtin functions:
* `name` (required): the language-facing name (as a member of the `builtins` attribute set) of the function.
* `implementation` (optional): a C++ expression specifying the implementation of the builtin.
  It must be a function of signature `void(EvalState &, PosIdx, Value * *, Value &)`.
  If not specified, defaults to `prim_${name}`.
* `renameInGlobalScope` (optional): whether the definition should be "hidden" in the global scope by prefixing its name with two underscores.
  If not specified, defaults to `true`.
* `args` (required): list containing the names of the arguments, as shown in the documentation.
  All arguments must be listed here since the function arity is derived as the length of this list.
* `experimental_feature` (optional): the user-facing name of the experimental feature which needs to be enabled for the builtin function to be available.
  If not specified, no experimental feature is required.

New builtin function definition files must be added to `lix/libexpr/builtins` and registered in the `builtin_definitions` list in `lix/libexpr/meson.build`.

## Builtin constants
The following metadata properties are supported for builtin constants:
* `name` (required): the language-facing name (as a member of the `builtins` attribute set) of the constant.
* `type` (required): the Nix language type of the constant; the C++ type is automatically derived.
* `constructorArgs` (optional): list of strings containing C++ expressions passed as arguments to the appropriate `Value` constructor.
  If the value computation is more complex, `implementation` can be used instead.
* `implementation` (required if `constructorArgs` is not specified): string containing a C++ expressing computing the value of the constant.
* `impure` (optional): whether the constant is considered impure.
  Impure constants are not available when pure evaluation mode is activated.
  Defaults to `false` when not specified.

New builtin constant definition files must be added to `lix/libexpr/builtin-constants` and registered in the `builtin_constant_definitions` list in `lix/libexpr/meson.build`.
