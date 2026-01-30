---
synopsis: "`nix develop` no longer ignores the env variable `SSL_CERT_FILE`"
cls: [5042]
category: "Improvements"
credits: [thubrecht]
---

Running `nix develop` and `nix print-dev-env` on shells that define the environment variable `SSL_CERT_FILE` now works correctly by exporting that variable inside the built shell.
