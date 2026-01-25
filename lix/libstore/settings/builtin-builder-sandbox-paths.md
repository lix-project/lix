---
name: builtin-builder-sandbox-paths
internalName: builtinBuilderSandboxPaths
type: PathSet
default: []
---
A list of paths bind-mounted into Nix sandbox environments when running
builtin builders. This is an advanced setting and should usually not be
changed; the default provided by the package build should be work fine.
