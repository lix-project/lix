---
synopsis: Enable high compress ratio zstd compression by default for binary caches uploads
issues: [fj#945]
cls: [4503]
category: "Breaking Changes"
credits: [horrors, raito]
---

The default compression method for binary cache uploads has been switched from
[`xz`](https://github.com/tukaani-project/xz) to
[`zstd`](https://github.com/facebook/zstd) to address performance and usability
issues related to modern hardware and high-speed connections.

## Why?

`xz` offers compression ratios but is single-threaded in our implementation and
very slow (~10-20 Mbps in our test), preventing full utilization of 100Mbps+
connections and significantly slowing decompression for end users.

Lix is a "compress once, decompress many" application: build farms can afford
to spend more time compressing to achieve a faster download transfer for the
end user. More importantly, it matters that all end users spend the least
amount of time decompressing.

## What about compression ratios?

`zstd` cannot achieve the same peaks as `xz`, nonetheless, `zstd` compression
level has been increased to level 12 by default to balance compression ratio
and performance.

## Synthetic test case data

* **xz** (default compression level) on a 4.4GB file: ~632MB (77s)
* **zstd** (level 12) on the same file: ~775MB (18s), 18% larger but 50% faster
* **zstd** (level 14): ~773MB (37s)
* **zstd** (level 16): ~735MB (66s)
