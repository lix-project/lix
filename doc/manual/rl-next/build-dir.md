---
synopsis: "Add a `build-dir` setting to set the backing directory for builds"
cls: 1514
prs: [gh#10303, gh#10312, gh#10883]
credits: [roberth, tomberek]
category: Improvements
---

`build-dir` can now be set in the Nix configuration to choose the backing directory for the build sandbox.
This can be useful on systems with `/tmp` on tmpfs, or simply to relocate large builds to another disk.

Also, `XDG_RUNTIME_DIR` is no longer considered when selecting the default temporary directory,
as it's not intended to be used for large amounts of data.
