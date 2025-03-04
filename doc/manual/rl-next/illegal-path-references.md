---
synopsis: Show illegal path references in fixed-outputs derivations
issues: [fj#530]
cls: [2726]
category: Fixes
credits: [thubrecht]
---

The error created when referencing a store path in a Fixed-Output Derivation is now more verbose, listing the offending paths.
This allows for better pinpointing where the issue might be.

An offender is the following derivation:

```nix
pkgs.stdenv.mkDerivation {
  name = "illegal-fod";

  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    cp -R ${pkgs.hello} $out
  '';

  outputHashMode = "recursive";
  outputHashAlgo = "sha256";
  outputHash = pkgs.lib.fakeHash;
}
```

The previous error shown would have been:

```
error: illegal path references in fixed-output derivation '/nix/store/rpq4m1y79s2nhs1hj7k47yiyykxykiqa-illegal-fod.drv'
```

and is now:

```
error: the fixed-output derivation '/nix/store/rpq4m1y79s2nhs1hj7k47yiyykxykiqa-illegal-fod.drv' must not reference store paths but 2 such references were found:
         /nix/store/1q8w6gl1ll0mwfkqc3c2yx005s6wwfrl-hello-2.12.1
         /nix/store/wn7v2vhyyyi6clcyn0s9ixvl7d4d87ic-glibc-2.40-36
```
