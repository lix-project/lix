---
name: currentTime
type: integer
constructorArgs: ['NixInt{time(0)}']
impure: true
---
Return the [Unix time](https://en.wikipedia.org/wiki/Unix_time) at first evaluation.
Repeated references to that name will re-use the initially obtained value.

Example:

```console
$ nix repl
Welcome to Nix 2.15.1 Type :? for help.

nix-repl> builtins.currentTime
1683705525

nix-repl> builtins.currentTime
1683705525
```

The [store path](@docroot@/glossary.md#gloss-store-path) of a derivation depending on `currentTime` will differ for each evaluation, unless both evaluate `builtins.currentTime` in the same second.
