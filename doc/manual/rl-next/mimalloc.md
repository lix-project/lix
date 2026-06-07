---
synopsis: "Use mimalloc for faster evaluation"
cls: [5645]
category: Features
credits: [getchoo, lovesegfault]
---

Lix now links with [mimalloc](https://github.com/microsoft/mimalloc),
replacing the system's default `malloc()` for all non-GC allocations.

This yields a **5–12% wall-clock improvement** on evaluation workloads,
ranging from `nix-instantiate hello` to `nix-env -qa` and full NixOS
configurations.

The allocator can be disabled at build time with `-Dmimalloc=disabled`,
or by passing the `useMimalloc = false` override to the `lix` package.
