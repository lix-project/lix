---
synopsis: Parse overflowing JSON number literals as floating‐point
issues: []
cls: [3919]
category: "Fixes"
credits: [emilazy]
---

Previously, `builtins.fromJSON "-9223372036854775809"` would
return a floating‐point number, while `builtins.fromJSON
"9223372036854775808"` would cause an evaluation error. This was
introduced with the banning of integer overflow in Lix 2.91; previously
the latter would result in C++ undefined behaviour. These cases are
now treated consistently with JSON’s model of a single numeric type,
and JSON number literals that do not fit in a Nix‐language integer
will be parsed as floating‐point numbers.
