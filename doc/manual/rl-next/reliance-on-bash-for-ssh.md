---
synopsis: Remove reliance on Bash for remote stores via SSH
issues: [fj#830, fj#805, fj#304]
cls: [3159]
category: "Fixes"
credits: [raito]
---

The pre-flight `echo started` handshake -- added years ago to catch race conditions -- has been removed.

After removal of connection sharing in Lix 2.93, it required a Bash-compatible shell and a standard `echo`, so it failed on:

  * builders protected by `ForceCommand` wrappers (e.g. `nix-remote-build`),
  * BusyBox / initrd images with no Bash,
  * hosts using non-POSIX shells such as Nushell.

The race the probe once addressed was tied to SSH connection-sharing -- since connection-sharing code has already been removed, the probe is now pointless.

Real connection or protocol errors are now left to SSH/Nix to report directly.

This is technically a breaking change if you had scripts that relied on the literal "started" which needs to be updated to rely on other signals, e.g., exit codes.
