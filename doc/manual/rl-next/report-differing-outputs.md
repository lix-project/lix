---
synopsis: "Reproducibility check builds now report all differing outputs"
cls: [2069]
category: Improvements
credits: [lheckemann]
---

`nix-build --check` allows rerunning the build of an already-built derivation to check that it produces the same output again.

If a multiple-output derivation with impure behaviour is built with `--check`, only the first output would be shown in the resulting error message (and kept for comparison):

```
error: derivation '/nix/store/4spy3nz1661zm15gkybsy1h5f36aliwx-python3.11-test-1.0.0.drv' may not be deterministic: output '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test
-1.0.0-dist' differs from '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist.check'
```

Now, all differing outputs are kept and reported:
```
error: derivation '4spy3nz1661zm15gkybsy1h5f36aliwx-python3.11-test-1.0.0.drv' may not be deterministic: outputs differ
         output differs: output '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist' differs from '/nix/store/ccqcp01zg18wp9iadzmzimqzdi3ll08d-python3.11-test-1.0.0-dist.check'
         output differs: output '/nix/store/yl59v08356i841c560alb0zmk7q16klb-python3.11-test-1.0.0' differs from '/nix/store/yl59v08356i841c560alb0zmk7q16klb-python3.11-test-1.0.0.check'
```
