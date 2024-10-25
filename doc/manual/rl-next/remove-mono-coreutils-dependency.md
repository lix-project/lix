---
synopsis: "Dependency on monolithic coreutils removed"
category: Development
cls: [2108]
credits: vigress8
---

Previously, the build erroneously depended on a `coreutils` binary, which requires `coreutils` to be built with a specific configuration. This was only used in one test and was not required to be a single binary. This dependency is removed now.
