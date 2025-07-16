---
synopsis: "`--keep-failed` chowns the build directory to the user that request the build"
issues: []
cls: []
category: Improvements
credits: [horrors]
---

Running a build with `--keep-failed` now chowns the temporary directory from the
builder user and group to the user that request the build if the build came from
a local user connected to the daemon. This makes inspecting failed derivations a
lot easier. On Linux the build directory made visible to the user will not be in
the same path as it was in the sandbox and continuing builds will usually break.
