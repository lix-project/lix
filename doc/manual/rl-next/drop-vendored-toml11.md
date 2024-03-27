---
synopsis: Stop vendoring toml11
cls: 675
---

We don't apply any patches to it, and vendoring it locks users into
bugs (it hasn't been updated since its introduction in late 2021).
