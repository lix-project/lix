---
synopsis: "Improved error messages for bad attr paths"
category: Improvements
cls: [2277, 2280]
credits: jade
---

Lix now includes much more detail when a bad attribute path is accessed at the command line:

```
 » nix eval -f '<nixpkgs>' lixVersions.lix_2_92
error: attribute 'lix_2_92' in selection path 'lixVersions.lix_2_92' not found
       Did you mean one of lix_2_90 or lix_2_91?
```

After:

```
 » nix eval --impure -f '<nixpkgs>' lixVersions.lix_2_92
error: attribute 'lix_2_92' in selection path 'lixVersions.lix_2_92' not found inside path 'lixVersions', whose contents are: { __unfix__ = «lambda @ /nix/store/hfz1qqd0z8amlgn8qwich1dvkmldik36-source/lib/fixed-points.nix:
447:7»; buildLix = «thunk»; extend = «thunk»; latest = «thunk»; lix_2_90 = «thunk»; lix_2_91 = «thunk»; override = «thunk»; overrideDerivation = «thunk»; recurseForDerivations = true; stable = «thunk»; }
       Did you mean one of lix_2_90 or lix_2_91?
```

This should avoid some unnecessary trips to the repl or to the debugger by giving some information about the value being selected on that was unexpected.
