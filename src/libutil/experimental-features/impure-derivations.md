---
name: impure-derivations
internalName: ImpureDerivations
---
Allow derivations to produce non-fixed outputs by setting the
`__impure` derivation attribute to `true`. An impure derivation can
have differing outputs each time it is built.

Example:

```
derivation {
  name = "impure";
  builder = /bin/sh;
  __impure = true; # mark this derivation as impure
  args = [ "-c" "read -n 10 random < /dev/random; echo $random > $out" ];
  system = builtins.currentSystem;
}
```

Each time this derivation is built, it can produce a different
output (as the builder outputs random bytes to `$out`).  Impure
derivations also have access to the network, and only fixed-output
or other impure derivations can rely on impure derivations. Finally,
an impure derivation cannot also be
[content-addressed](#xp-feature-ca-derivations).

This is a more explicit alternative to using [`builtins.currentTime`](@docroot@/language/builtin-constants.md#builtins-currentTime).
