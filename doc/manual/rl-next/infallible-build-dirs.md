---
synopsis: "Fallback to safe temp dir when build-dir is unwritable"
issues: [fj#876]
cls: [3501]
category: "Fixes"
credits: ["raito", "horrors"]
---

Non-daemon builds started failing with a permission error after introducing the `build-dir` option:

```
$ nix build --store ~/scratch nixpkgs#hello --rebuild
error: creating directory '/nix/var/nix/builds/nix-build-hello-2.12.2.drv-0': Permission denied
```

This happens because:

1. These builds are not run via the daemon, which owns `/nix/var/nix/builds`.
2. The user lacks permissions for that path.

We considered making `build-dir` a store-level option and defaulting it to `<chroot-root>/nix/var/nix/builds` for chroot stores, but opted instead for a fallback: if the default fails, Nix now creates a safe build directory under `/tmp`.

To avoid CVE-2025-52991, the fallback uses an extra path component between `/tmp` and the build dir.

**Note**: this fallback clutters `/tmp` with build directories that are not cleaned up. To prevent this, explicitly set `build-dir` to a path managed by Lix, even for local workloads.
