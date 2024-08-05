---
name: allow-symlinked-store
internalName: allowSymlinkedStore
type: bool
default: false
---
If set to `true`, Lix will stop complaining if the store directory
(typically /nix/store) contains symlink components.

This risks making some builds "impure" because builders sometimes
"canonicalise" paths by resolving all symlink components. Problems
occur if those builds are then deployed to machines where /nix/store
resolves to a different location from that of the build machine. You
can enable this setting if you are sure you're not going to do that.
