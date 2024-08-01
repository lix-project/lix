---
synopsis: "clang-tidy support"
cls: 1697
issues: fj#147
credits: jade
category: Development
---

`clang-tidy` can be used to lint Lix with a limited set of lints using `ninja -C build clang-tidy` and `ninja -C build clang-tidy-fix`.
In practice, this fixes the built-in meson rule that was used the same as above being broken ever since precompiled headers were introduced.
