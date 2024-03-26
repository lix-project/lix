---
synopsis: reintroduce shortened `-E` form for `--expr` to new CLI
cls: 605
---

In the past, it was possible to supply a shorter `-E` flag instead of fully
specifying `--expr` every time you wished to provide an expression that would
be evaluated to produce the given command's input. This was retained for the
`--file` flag when the new CLI utilities were written with `-f`, but `-E` was
dropped.

We now restore the `-E` short form for better UX. This is most useful for
`nix eval` but most any command that takes an Installable argument should benefit
from it as well.
