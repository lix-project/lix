---
synopsis: "Hash mismatch diagnostics for fixed-output derivations include the URL"
cls: [1536]
credits: [jade]
category: Improvements
---

Now, when building fixed-output derivations, Lix will guess the URL that was used in the derivation using the `url` or `urls` properties in the derivation environment.
This is a layering violation but making these diagnostics tractable when there are multiple instances of the `AAAA` hash is too significant of an improvement to pass it up.

```
error: hash mismatch in fixed-output derivation '/nix/store/sjfw324j4533lwnpmr5z4icpb85r63ai-x1.drv':
        likely URL: https://meow.puppy.forge/puppy.tar.gz
         specified: sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
            got:    sha256-a1Qvp3FOOkWpL9kFHgugU1ok5UtRPSu+NwCZKbbaEro=
```
