---
synopsis: "`ssh-ng` stores now forward `keep-going` to the remote"
cls: [6536]
category: Features
credits: [raito]
issues: [fj#1295]
---

Using an `ssh-ng` store as the main store, e.g. `nix-build --store
ssh-ng://builder --eval-store auto --keep-going`, now forwards `keep-going` to
the remote daemon.

Builds that happen on the remote store therefore keep going after a derivation
fails instead of aborting every other in-flight build with it.

`keep-going` was previously honoured for local builds and remote builders (i.e.
`--builders` option).

`keep-going` requires a special protocol Lix feature that is available since
Lix 2.96 and will require both ends to use Lix 2.96 or newer to be recognised.
This will not have impact on older Lix versions and degrade gracefully.

This does not affect `ssh://` stores which are slated for removal in the next
release as `ssh-ng://` will replace them.
