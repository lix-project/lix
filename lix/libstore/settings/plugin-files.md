---
name: plugin-files
internalName: pluginFiles
settingType: PluginFilesSetting
default: []
---
A list of plugin files to be loaded by Nix. Each of these files will
be dlopened by Nix, allowing them to affect execution through static
initialization. In particular, these plugins may construct static
instances of RegisterPrimOp to add new primops or constants to the
expression language, RegisterStoreImplementation to add new store
implementations, RegisterCommand to add new subcommands to the `nix`
command, and RegisterSetting to add new nix config settings. See the
constructors for those types for more details.

Warning! These APIs are inherently unstable and may change from
release to release.

Since these files are loaded into the same address space as Nix
itself, they must be DSOs compatible with the instance of Nix
running at the time (i.e. compiled against the same headers, not
linked to any incompatible libraries). They should not be linked to
any Lix libs directly, as those will be available already at load
time.

If an entry in the list is a directory, all files in the directory
are loaded as plugins (non-recursively).
