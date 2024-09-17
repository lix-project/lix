---
synopsis: readline support removed
cls: [1885]
category: Packaging
credits: [9999years]
---

Support for building Lix with [`readline`][readline] instead of
[`editline`][editline] has been removed. `readline` support hasn't worked for a
long time (attempting to use it would lead to build errors) and would make Lix
subject to the GPL if it did work. In the future, we're hoping to replace
`editline` with [`rustyline`][rustyline] for improved ergonomics in the `nix
repl`.

[readline]: https://en.wikipedia.org/wiki/GNU_Readline
[editline]: https://github.com/troglobit/editline
[rustyline]: https://github.com/kkawakam/rustyline
