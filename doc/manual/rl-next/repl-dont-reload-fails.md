---
synopsis: "The REPL no longer considers failed loads for `:reload`"
cls: [4864, 4865, 4700, 4889]
category: "Fixes"
issues: [fj#50]
credits: [raito, Qyriad]
---

The [REPL](@docroot@/command-ref/new-cli/nix3-repl.md) allows "loading" files, flakes, and expressions into the environment, with the commands `:load`/`:l`, `:load-flake`/`:lf`, and `:add`/`:a` respectively.
The results of those stay in the environment as-is even if their sources change, until the `:reload` command is used.
However `:reload` would re-perform *all* instances of `:l`/`:lf`/`:a`, meaning you would get things like this:

```nix
nix-repl> :l /tmp/texting.nix
error: getting status of '/tmp/texting.nix': No such file or directory
# oops, typo.
nix-repl> :l /tmp/testing.nix

# Do some stuff…

nix-repl> :reload
error: getting status of '/tmp/texting.nix': No such file or directory
```

This is pretty silly, but also *incredibly* annoying, as it would stop there and *not* reload the correct files anymore.
This effectively meant typoing any of the load commands would make `:reload` useless for the rest of the entire `nix repl` session!

This has been fixed, so now only *successful* loads count towards `:reload`.
