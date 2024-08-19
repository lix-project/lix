---
synopsis: Replace regex engine with boost::regex
issues: [fj#34, fj#476]
cls: [1821]
category: Fixes
credits: [sugar]
---

Previously, the C++ standard regex expression library was used, the
behaviour of which varied depending on the platform. This has been
replaced with the Boost regex library, which works identically across
platforms.

The visible behaviour of the regex functions doesn't change. While
the new library has more features, Lix will reject regular expressions
using them.

This also fixes regex matching reporting stack overflow when matching
on too much data.

Before:

    nix-repl> builtins.match ".*" (
                builtins.concatStringsSep "" (
                  builtins.genList (_: "a") 1000000
                )
              )
    error: stack overflow (possible infinite recursion)

After:

    nix-repl> builtins.match ".*" (
                builtins.concatStringsSep "" (
                  builtins.genList (_: "a") 1000000
                )
              )
    [ ]
