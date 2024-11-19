---
name: cores
internalName: buildCores
type: unsigned int
defaultExpr: 'getDefaultCores()'
defaultText: '*machine-specific*'
aliases: [build-cores]
---
Sets the value of the `NIX_BUILD_CORES` environment variable in the
invocation of builders. Builders can use this variable at their
discretion to control the maximum amount of parallelism. For
instance, in Nixpkgs, if the derivation attribute
`enableParallelBuilding` is set to `true`, the builder passes the
`-jN` flag to GNU Make. It can be overridden using the `--cores`
command line switch and defaults to `1`. The value `0` means that
the builder should use all available CPU cores in the system.
