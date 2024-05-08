---
synopsis: Enforce syscall filtering and no-new-privileges on Linux
cls: 1063
category: Breaking Changes
credits: alois31
---

In order to improve consistency of the build environment, system call filtering and no-new-privileges are now unconditionally enabled on Linux.
The `filter-syscalls` and `allow-new-privileges` options which could be used to disable these features under some circumstances have been removed.

In order to support building on architectures without libseccomp support, the option to disable syscall filtering at build time remains.
However, other uses of this option are heavily discouraged, since it would reduce the security of the sandbox substantially.
