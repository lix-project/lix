---
synopsis: Change `nix-build -o ""` to behave like `--no-out-link`
cls: [2103]
category: Fixes
credits: lilyball
---

[`nix-build`](@docroot@/command-ref/nix-build.md)now treats <code>[--out-link](@docroot@/command-ref/nix-build.md#opt-out-link) ''</code>
the same as [`--no-out-link`](@docroot@/command-ref/nix-build.md#opt-no-out-link). This matches
[`nix build`](@docroot@/command-ref/new-cli/nix3-build.md) behavior. Previously when building the default output it
would have resulted in throwing an error saying the current working directory already exists, and when building any
other output it would have resulted in a symlink starting with a hyphen such as `-doc`, which is a footgun for
terminal commands.
