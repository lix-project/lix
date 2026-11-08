---
synopsis: "`log-lines` and other per-client preferences now work for untrusted users"
cls: [6544]
category: Features
credits: [raito]
issues: [fj#1273, fj#1197]
---

Settings that only affect the client's own connection to a Lix daemon are now
forwarded through a dedicated *client preferences* channel instead of the
configuration override path requiring to be a trusted user.

This means, e.g. `nix build --log-lines 5` is now honoured when building
through an untrusted local daemon or an untrusted `ssh-ng` remote builder,
instead of being dropped with `Ignoring the client-specified setting
'log-lines', because it is a restricted setting`.

The channel is only used between two Lix implementations: both ends must be
2.97 or newer. A newer client talking to an older Lix daemon, or vice versa,
falls back to the previous behavior.
