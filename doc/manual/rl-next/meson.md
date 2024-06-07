---
synopsis: Lix is built with meson
# and many more
cls: [580, 627, 628, 707, 711, 712, 719]
credits: [Qyriad, horrors, jade, 9999years, winter]
category: Packaging
---

Lix is built exclusively with the meson build system thanks to a huge team-wide
effort, and the legacy `make`/`autoconf` based build system has been removed
altogether. This improves maintainability of Lix, enables things like saving
20% of compile times with precompiled headers, and generally makes the build
less able to produce obscure incremental compilation bugs.

Non-Nix-based downstream packaging needs rewriting accordingly.
