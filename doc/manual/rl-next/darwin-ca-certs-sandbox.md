---
synopsis: "Fix CA certificates access in macOS sandboxed builds"
cls: [2869]
category: Fixes
credits: [WeetHet]
---

Fixed an issue on macOS where fixed-output derivations that needed network access could not access the CA certificate.
The sandbox profile now explicitly allows access to the configured CA file when a fixed output derivation is built.

This fixes `pkgs.fetchgit`, `fetchCargoVendor` and many others when run with `sandbox = true`
