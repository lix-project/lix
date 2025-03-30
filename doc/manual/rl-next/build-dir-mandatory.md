---
synopsis: "`build-dir` no longer defaults to `temp-dir`"
cls: [3453]
category: "Fixes"
credits: [horrors]
---

The directory in which temporary build directories are created no longer defaults
to the value of the `temp-dir` setting to avoid builders making their directories
world-accessible. This behavior has been used to escape the build sandbox and can
cause build impurities even when not used maliciously. We now default to `builds`
in `NIX_STATE_DIR` (which is `/nix/var/nix/builds` in the default configuration).
