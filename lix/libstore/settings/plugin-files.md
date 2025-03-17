---
name: plugin-files
internalName: pluginFiles
settingType: PluginFilesSetting
default: []
---
A list of plugin files to be loaded by Lix.

Each of these files will be `dlopen`ed by Lix, allowing them to affect execution by registering various entities in Lix.
After the plugins are loaded, they will have the function within them with signature `extern "C" void nix_plugin_entry(void)` called if it is defined.

If an entry in the list is a directory, all files in the directory are loaded as plugins (non-recursively).

FIXME(jade): We should provide a `nix_plugin_finalize()` that gets called at some point in teardown for use cases like nix-otel which need to be able to cleanup, flush things to network, etc, on exit without having to do that from life-after-main().

In particular, these plugins may:
- Construct static instances of `RegisterPrimOp` to add new primops or constants to the expression language (FIXME: will be replaced with an explicit function).
- Add new store implementations with `StoreImplementations::add`.
- Construct static instances of `RegisterCommand` to add new subcommands to the `nix` command (FIXME: will be replaced with an explicit function).
- Construct static instances of `Setting` to add new Lix settings (FIXME: will be replaced with an explicit function).

See the documentation for those symbols for more details.
Note all the FIXMEs above: Lix is removing its usages of static initializers, see <https://git.lix.systems/lix-project/lix/issues/359>.

Warning! These APIs are inherently unstable and may change in minor versions.
It's recommended to, if you *are* relying on Lix's unstable C++ API, develop against Lix `main`, run `main` yourself, and be active in Lix development.

Since these files are loaded into the same address space as Lix itself, they must be DSOs compatible with the instance of Lix running at the time (i.e. compiled against the same headers, not linked to any incompatible libraries, produced by the same nixpkgs).

It's recommended that this setting *not* be used in `nix.conf` since it is almost always the case that there are multiple versions of the Nix implementation on a machine.
In particular, CppNix (still true in 2.26 as of this writing) considers plugin load failure to be a hard error unlike Lix (since pre-2.90), which means that putting `plugin-files` in `nix.conf` causes random `nix` execution failures.
Prefer instead to wrap the `nix` command using either the `NIX_CONFIG` environment variable or `--option plugin-files`.

Plugins should not be linked to any Lix libs directly, as those will be available already at load time (FIXME: is it an actual problem if they are, assuming that there are not version mismatches?).
