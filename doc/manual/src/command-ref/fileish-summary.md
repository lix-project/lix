<!--
  File-ish argument syntax summary.
  This file gets included into pages like nix-build.md and nix-instantiate.md, and each individual page that includes
  this also links to nix-build.md for the full explanation.
-->
- A normal filesystem path, like `/home/meow/nixfiles/default.nix`
  - Or a directory, like `/home/meow/nixfiles`, equivalent to above
- A single lookup path, like `<nixpkgs>` or `<nixos>`
- A URL to a tarball, like `https://github.com/NixOS/nixpkgs/archive/refs/heads/release-23.11.tar.gz`
- A [flakeref](@docroot@/command-ref/new-cli/nix3-flake.md#flake-references), introduced by the prefix `flake:`, like `flake:git+https://git.lix.systems/lix-project/lix`
- A *nixpkgs* channel tarball name, introduced by the prefix `channel:`, like `channel:nixos-unstable`.
  - This uses a hard-coded URL pattern and is *not* related to the subscribed channels managed by the [nix-channel](@docroot@/command-ref/nix-channel.md) command.
