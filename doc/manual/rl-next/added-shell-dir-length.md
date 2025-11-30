---
synopsis: "Nix shells' $NIX_BUILD_TOP are shorter"
cls: [4663]
issues: [fj#1044]
category: "Fixes"
credits: [raito]
---

Following the changes in 2.94.0 to shorten build directory paths, aimed at [resolving UNIX domain socket length issues](https://gerrit.lix.systems/c/lix/+/4168/13) and [improving nix-shell](https://git.lix.systems/lix-project/lix/issues/940), we inadvertently introduced an excessively long path for the `$NIX_BUILD_TOP` environment variable used by Nix shells (their effective temporary `/build` directory).

To fix this, we replaced the `build-top-$HASH` directory name with simply `build-top`, reducing these paths by at least 30 characters.

We also added a test to ensure that Nix shells do not introduce more than 50 extra characters relative to their base directory (e.g., `/tmp` when `$TMPDIR` is not set).
