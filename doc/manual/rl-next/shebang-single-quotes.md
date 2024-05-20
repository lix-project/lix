---
synopsis: Allow single quotes in nix-shell shebangs
prs: 8470
credits: [ncfavier, horrors]
category: Improvements
---

Example:

```bash
#! /usr/bin/env nix-shell
#! nix-shell -i bash --packages 'terraform.withPlugins (plugins: [ plugins.openstack ])'
```
