---
synopsis: "`post-build-hook` only receives settings that are set"
cls: [2800]
issues: [fj#739]
category: Fixes
credits: jade
---
If one is using `post-build-hook` to upload paths to a cache, it used to be broken if CppNix was used inside the script, since CppNix would fail about unsupported configuration option values in some of Lix's defaults.
This is because `post-build-hook` receives the settings of the nix daemon in the `NIX_CONFIG` environment variable.
Now Lix only emits overridden settings to `post-build-hook` invocations, which fixes this issue in the majority of cases: where the configuration is not explicitly incompatible.
