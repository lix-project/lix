---
synopsis: "`lix foo` now invokes `lix-foo` from PATH"
cls: [2119]
category: Features
credits: raito
---

Lix introduces the ability to extend the Nix command line by adding custom
binaries to the `PATH`, similar to how Git integrates with other tools. This
feature allows developers and end users to enhance their workflow by
integrating additional functionalities directly into the Nix CLI.

#### Examples

For example, a user can create a custom deployment tool, `lix-deploy-tool`, and
place it in their `PATH`. This allows them to execute `lix deploy-tool`
directly from the command line, streamlining the process of deploying
applications without needing to switch contexts or use separate commands.

#### Limitations

For now, autocompletion is supported to discover new custom commands, but the
documentation will not render them. Argument autocompletion of the custom
command is not supported either.

This is also locked behind a new experimental feature called
`lix-custom-sub-commands` to enable developing all the required features.

Only the top-level `lix` command can be extended, this is an artificial
limitation for the time being until we flesh out this feature.

#### Outline

In the future, this feature may pave the way for moving the Flake subcommand
line to its own standalone binary, allowing for a more focused approach to
managing Nix Flakes while letting the community explore alternatives to
dependency management.
