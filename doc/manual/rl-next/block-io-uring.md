---
synopsis: "Block io_uring in the Linux sandbox"
cls: 1611
credits: alois31
category: Breaking Changes
---

The io\_uring API has the unfortunate property that it is not possible to selectively decide which operations should be allowed.
This, together with the fact that new operations are routinely added, makes it a hazard to the proper function of the sandbox.

Therefore, any access to io\_uring has been made unavailable inside the sandbox.
As such, attempts to execute any system calls forming part of this API will fail with the error `ENOSYS`, as if io\_uring support had not been configured into the kernel.
