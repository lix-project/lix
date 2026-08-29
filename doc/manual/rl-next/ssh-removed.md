---
synopsis: "`ssh` stores are no longer supported"
cls: [6418]
category: Breaking Changes
credits: [horrors]
---

`ssh` remote store are no longer supported, neither for remote builds not for anything else.
commands that used `ssh` URIs internally (i.e. `nix-copy-closure`) no longer do so, and thus
may fail due to behavioral differences of `ssh` and `ssh-ng`. for example closure uploads to
`ssh` URIs default to ***NOT*** checking signatures of the uploaded closure while uploads to
`ssh-ng` URIs do. this improves security of `nix-copy-closure`, but it also makes copying an
unsigned closure impossible for untrusted users. we *do not* intend to restore this behavior
since it could have been used to poison any nonexistent input-addressed paths on the remote.
