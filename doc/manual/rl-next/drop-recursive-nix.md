---
synopsis: "Removal of the `recursive-nix` experimental feature"
issues: [fj#767]
cls: [2872]
category: "Breaking Changes"
credits: ["raito"]
---

The `recursive-nix` experimental feature and all associated code have been removed.

`recursive-nix` enabled running Nix operations (like evaluations and builds) *inside* a derivation builder. This worked by spawning a temporary Nix daemon socket within the build environment, allowing the derivation to emit outputs that appeared in the outer store. This was primarily used to prototype **dynamic derivations** (dyndrvs), where build plans are generated on-the-fly during a build.

However, this approach introduced critical issues:

- It entrenched the legacy Nix daemon protocol as part of the derivation ABI, which is a blocker for future stabilization.
- It imposed tight coupling between sandbox setup code and knowledge of Nix internals, complicating refactoring and long-term maintenance.
- It was never intended to be the final design for dynamic derivations. The original Nix implementation team, who are leading dyndrv development, have agreed it will be replaced (likely via `varlink` or similar) before any stabilization.
- There is currently no known usage of `recursive-nix` on `lix` or elsewhere **in production**.

If you're using `recursive-nix` for something niche or experimental, we'd love to hear from you on the RFD issue.
You can still run `nix` inside a builder manually if needed — including with isolated user namespaces and fake stores — but the special daemon-handshake machinery is gone.

This removal unblocks several important internal cleanups.
