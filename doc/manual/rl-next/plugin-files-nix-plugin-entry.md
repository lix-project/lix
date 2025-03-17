---
synopsis: "Add `nix_plugin_entry` entry point for plugins"
issues: [fj#740, fj#359]
prs: [gh#8699]
cls: [2826]
category: Development
credits: ["jade", "yorickvp"]
---

Plugins are an exceptionally rarely used feature in Lix, but they are important as a prototyping tool for code destined for Lix itself, and we want to keep supporting them as a low-maintenance-cost feature.
As part of the overall move towards getting rid of static initializers for stability and predictability reasons, we added an explicit `nix_plugin_entry` function like CppNix has, which is called immediately after plugin load, if present.
This makes control flow more explicit and allows for easily registering things that have had their static initializer registration classes removed.
