---
synopsis: "Fix curl error `A value or data field grew larger than allowed`"
cls: [2780]
category: Fixes
credits: horrors
---

2.92 started using curl-provided HTTP decompression code, but it as discovered
that curl has [a bug] that effectively breaks its decompression code on HTTP/2
transfers. We have partially rolled back our changes and no longer use builtin
decompression methods provided by curl, but have kept the refusal of bzip2 and
xz content encodings introduced with 2.92 since they are not in the HTTP spec.

[a bug]: https://git.lix.systems/lix-project/lix/issues/662
