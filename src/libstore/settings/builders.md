---
name: builders
internalName: builders
type: std::string
defaultExpr: '"@" + nixConfDir + "/machines"'
defaultText: '`@/etc/nix/machines`'
---
A semicolon-separated list of build machines.
For the exact format and examples, see [the manual chapter on remote builds](../advanced-topics/distributed-builds.md)

Defaults to `@$NIX_CONF_DIR/machines`.
The default shown below is only accurate when the value of `NIX_CONF_DIR` has not been overridden at build time or using the environment variable.
