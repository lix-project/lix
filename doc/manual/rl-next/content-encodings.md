---
synopsis: "Drop support for `xz` and `bzip2` Content-Encoding"
category: Miscellany
cls: [2134]
credits: horrors
---

Lix no longer supports the non-standard HTTP Content-Encoding values `xz` and `bzip2`.
We do not expect this to cause any problems in practice since these encodings *aren't*
standard, and any server delivering them anyway without being asked to is already well
and truly set on the path of causing inexplicable client breakages.

Lix's ability to decompress files compressed with `xz` or `bzip2` is unaffected. We're
only bringing Lix more in line with the HTTP standard; all post-transfer data handling
remains as it was before.
