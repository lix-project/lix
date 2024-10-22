---
synopsis: "transfers no longer allow arbitrary url schemas"
category: Breaking Changes
cls: [2106]
credits: horrors
---

Lix no longer allows transfers using arbitrary url schemas. Only `http://`, `https://`, `ftp://`, `ftps://`, and `file://` urls are supported going forward. This affects `builtins.fetchurl`, `<nix/fetchurl.nix>`, transfers to and from binary caches, and all other uses of the internal file transfer code. Flake inputs using multi-protocol schemas (e.g. `git+ssh`) are not affected as those use external utilities to transfer data.

The `s3://` scheme is not affected at all by this change and continues to work if S3 support is built into Lix.
