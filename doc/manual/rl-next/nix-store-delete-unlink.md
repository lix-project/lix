---
synopsis: nix store delete can now unlink a GC root before deleting its closure
cls: [4660]
category: Improvements
credits: [Qyriad]
---

Ever build something, and then you want to delete it and whatever dependencies it downloaded?
Before you had to resolve the `result` symlink and copy it, then delete it, *then* `nix store delete --delete-closure --skip-live` on the path you copied.
Now you can just pass `--unlink` and the `result` symlink itself.
