---
synopsis: "Paralellise `nix store sign` using a thread pool"
issues: [399]
cls: [2606]
category: Fixes
credits: [Lunaphied]
---

`nix store sign` with a large collection of provided paths (such as when using with `--all`) has historically
signed these paths serially. Taking extreme amounts of time when preforming operations such as fixing binary
caches. This has been changed. Now these signatures are performed using a thread pool like `nix store copy-sigs`.
