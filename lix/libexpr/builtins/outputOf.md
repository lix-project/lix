---
name: outputOf
args: [derivation-reference, output-name]
experimentalFeature: dynamic-derivations
---
Return the output path of a derivation, literally or using a placeholder if needed.

If the derivation has a statically-known output path (i.e. the derivation output is input-addressed, or fixed content-addressed), the output path will just be returned.
But if the derivation is content-addressed or if the derivation is itself not-statically produced (i.e. is the output of another derivation), a placeholder will be returned instead.

*`derivation reference`* must be a string that may contain a regular store path to a derivation, or may be a placeholder reference. If the derivation is produced by a derivation, you must explicitly select `drv.outPath`.
This primop can be chained arbitrarily deeply.
For instance,

```nix
builtins.outputOf
  (builtins.outputOf myDrv "out)
  "out"
```

will return a placeholder for the output of the output of `myDrv`.

This primop corresponds to the `^` sigil for derivable paths, e.g. as part of installable syntax on the command line.
