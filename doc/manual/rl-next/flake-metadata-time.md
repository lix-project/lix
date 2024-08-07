---
synopsis: "`nix flake metadata` prints modified date"
cls: 1700
credits: jade
category: Improvements
---

Ever wonder "gee, when *did* I update nixpkgs"?
Wonder no more, because `nix flake metadata` now simply tells you the times every locked flake input was updated:

```
<...>
Description:   The purely functional package manager
Path:          /nix/store/c91yi8sxakc2ry7y4ac1smzwka4l5p78-source
Revision:      c52cff582043838bbe29768e7da232483d52b61d-dirty
Last modified: 2024-07-31 22:15:54
Inputs:
├───flake-compat: github:edolstra/flake-compat/0f9255e01c2351cc7d116c072cb317785dd33b33
│   Last modified: 2023-10-04 06:37:54
├───nix2container: github:nlewo/nix2container/3853e5caf9ad24103b13aa6e0e8bcebb47649fe4
│   Last modified: 2024-07-10 13:15:56
├───nixpkgs: github:NixOS/nixpkgs/e21630230c77140bc6478a21cd71e8bb73706fce
│   Last modified: 2024-07-25 11:26:27
├───nixpkgs-regression: github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2
│   Last modified: 2022-01-24 11:20:45
└───pre-commit-hooks: github:cachix/git-hooks.nix/f451c19376071a90d8c58ab1a953c6e9840527fd
    Last modified: 2024-07-15 04:21:09
```
