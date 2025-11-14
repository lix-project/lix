---
synopsis: Lix supports HTTP/3 behind `--http3`
issues: [fj#1033]
category: "Features"
credits: [raito, horrors]
---

Lix now supports HTTP/3 for file transfers when the linked curl version
supports it.

By default, HTTP/3 is disabled notably due to performance issues reported in
mid-2024. [More details
here](https://daniel.haxx.se/blog/2024/06/10/http-3-in-curl-mid-2024/).

As of 2025-11-14, [NixOS official cache](https://cache.nixos.org) supports
HTTP/3 via Fastly. [More info
here](https://github.com/NixOS/infra/commit/157fa70e46afbd6338a32407be461fce05c57bf8).

To enable HTTP/3:

* Use `--http3` for individual transfers.
* Add `http3 = true` in your Nix configuration for permanent activation.

To disable it, use `--no-http3`.

**Note**:

* `--no-http2 --http3` will still enable both HTTP/2 and HTTP/3.
* `--http2 --http3` will prioritize HTTP/3 and fall back to HTTP/2 (and then
  HTTP/1.1).

These are current CLI limitations. In the future, we plan to replace `--httpX`
options with `--max-http-version [1,2,3]` for easier version selection in Lix
transfers.
