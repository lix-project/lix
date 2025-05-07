---
synopsis: "Report GC statistics correctly"
issues: []
cls: [3188]
category: Fixes
credits: [lheckemann]
---

Deleting specific paths using `nix-store --delete` or `nix store delete` previously did
not report statistics correctly when some of the paths could not be deleted, even if
others were deleted:

```
$ nix store delete /nix/store/9bwryidal9q3g91cjm6xschfn4ikd82q-hello-2.12.1 --delete-closure -v
finding garbage collector roots...
deleting '/nix/store/9bwryidal9q3g91cjm6xschfn4ikd82q-hello-2.12.1'
0 store paths deleted, 0.00 MiB freed
error: Cannot delete some of the given paths because they are still alive. Paths not deleted:
         k9bxzr1l92r5y6mihrkbpbr3fmc8qszx-libidn2-2.3.8
         mbx9ii53lzjlrsnlrfmzpwm33ynljwdn-libunistring-1.3
         rf8hcy6bldxdqc0g6q1dcka1vh47x69s-xgcc-14.2.1.20250322-libgcc
         vbrdc5wgzn0w1zdp10xd2favkjn5fk7y-glibc-2.40-66
       To find out why, use nix-store --query --roots and nix-store --query --referrers.
```
