---
synopsis: "Builders are always started in a fresh cgroup namespace"
cls: [1996]
category: Breaking Changes
credits: raito
---

If you haven't enabled the experimental `cgroups` feature, Nix previously launched builder processes in new namespaces but did not create new cgroup namespaces. As a result, derivations could access and observe the parent cgroup namespace.

Although this update introduces a breaking change, it ensures that all derivations now start in a fresh cgroup namespace by default. This reduces potential impurities observable within the sandbox, improving the likelihood of reproducible builds across different environments.
