---
synopsis: "Flakes/restrict-eval no longer allow reading contents of impure paths"
category: Fixes
credits: [horrors]
---

Flakes and `--restrict-eval` now correctly restrict access to paths as intended.
In prior versions since at least 2.18, `nix eval --raw .#lol` for the following flake didn't throw an error and acted as if `--impure` was passed.

Thanks to the person who reported this for telling us about it.
This was handled as a low-severity security bug, but is not a violation of the [documented security model](../installation/multi-user.md) as untrusted Nix code should be assumed to have the privileges of the user running the evaluator.
To report a security bug, email a report to `security at lix dot systems`.

```nix
{
  inputs = {};
  outputs = {...}: {
    lol = builtins.readFile "${/etc/passwd}";
  };
}
```
