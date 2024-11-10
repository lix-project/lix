---
name: accept-flake-config
internalName: acceptFlakeConfig
type: AcceptFlakeConfig
defaultExpr: AcceptFlakeConfig::Ask
defaultText: '`ask`'
experimentalFeature: flakes
---
Whether to accept Lix configuration from the `nixConfig` attribute of
a flake. Doing so as a trusted user allows Nix flakes to gain root
access on your machine if they set one of the several
trusted-user-only settings that execute commands as root.

If set to `true`, such configuration will be accepted without asking;
this is almost always a very bad idea. Setting this to `ask` will
prompt the user each time whether to allow a certain configuration
option set this way, and offer to optionally remember their choice.
When set to `false`, the configuration will be automatically
declined.

See [multi-user installations](@docroot@/installation/multi-user.md)
for more details on the Lix security model.
