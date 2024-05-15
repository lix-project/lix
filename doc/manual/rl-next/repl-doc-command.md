---
synopsis: Experimental REPL support for documentation comments using `:doc`
cls: 564
category: Features
credits: [Lunaphied, jade]
significance: significant
---

Using `:doc` in the REPL now supports showing documentation comments when defined on a function.

Previously this was only able to document builtins, however it now will show comments defined on a lambda as well.

This support is experimental and relies on an embedded version of [nix-doc](https://github.com/lf-/nix-doc).

The logic also supports limited Markdown formatting of doccomments and should easily support any [RFC 145](https://github.com/NixOS/rfcs/blob/master/rfcs/0145-doc-strings.md)
compatible documentation comments in addition to simple commented documentation.
