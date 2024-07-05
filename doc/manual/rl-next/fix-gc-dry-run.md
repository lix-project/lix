---
synopsis: Fix nix-collect-garbage --dry-run
issues: [fj#432]
cls: [1566]
category: Fixes
credits: [quantumjump]
---

`nix-collect-garbage --dry-run` did not previously give any output - it simply
exited without even checking to see what paths would be deleted.

```
$ nix-collect-garbage --dry-run
$
```

We updated the behaviour of the flag such that instead it prints out how many
paths it *would* delete, but doesn't actually delete them.

```
$ nix-collect-garbage --dry-run
finding garbage collector roots...
determining live/dead paths...
...
<nix store paths>
...
2670 store paths deleted, 0.00MiB freed
$
```
