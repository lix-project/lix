---
synopsis: Remove support for daemon protocols before 2.18
issues: [fj#510]
cls: [3249]
significance: significant
category: "Breaking Changes"
credits: [horrors]
---

Support for daemon wire protocols belonging to Nix 2.17 or older have been
removed. This impacts clients connecting to the local daemon socket or any
remote builder configured using the `ssh-ng` protocol. Builders configured
with the `ssh` protocol are still accessible from clients such as Nix 2.3.
Additionally Lix will not be able to connect to an old daemon locally, and
remote build connections to old daemons is likewise limited to `ssh` urls.

We have decided to take this step because the old protocols are very badly
tested (if at all), maintenance overhead is high, and a number of problems
with their design makes it infeasible to remain backwards compatible while
we move Lix to a more modern RPC mechanism with better versioning support.
