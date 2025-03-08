---
synopsis: '`--skip-live` for path deletion'
issues: []
cls: [2778]
category: Improvements
credits: [lheckemann]
---

`nix-store --delete` and `nix store delete` now support a
`--skip-live` option and a `--delete-closure` option.

This makes custom garbage-collection logic a lot easier to implement
and experiment with:

- Paths known to be large can be thrown at `nix store delete` without
  having to manually filter out those that are still reachable from a
  root, e.g.
  `nix store delete /nix/store/*mbrola-voices*`

- The `--delete-closure` option allows extending this to paths that are
  not large themselves but do have a large closure size, e.g.
  `nix store delete /nix/store/*nixos-system-gamingpc*`.

- Other heuristics like atime-based deletion can be applied more
  easily, because `nix store delete` once again takes over the task of
  working out which paths can't be deleted.
