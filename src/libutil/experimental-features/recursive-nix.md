---
name: recursive-nix
internalName: RecursiveNix
---
Allow derivation builders to call Nix, and thus build derivations
recursively.

Example:

```
with import <nixpkgs> {};

runCommand "foo"
  {
     buildInputs = [ nix jq ];
     NIX_PATH = "nixpkgs=${<nixpkgs>}";
  }
  ''
    hello=$(nix-build -E '(import <nixpkgs> {}).hello.overrideDerivation (args: { name = "recursive-hello"; })')

    mkdir -p $out/bin
    ln -s $hello/bin/hello $out/bin/hello
  ''
```

An important restriction on recursive builders is disallowing
arbitrary substitutions. For example, running

```
nix-store -r /nix/store/kmwd1hq55akdb9sc7l3finr175dajlby-hello-2.10
```

in the above `runCommand` script would be disallowed, as this could
lead to derivations with hidden dependencies or breaking
reproducibility by relying on the current state of the Nix store. An
exception would be if
`/nix/store/kmwd1hq55akdb9sc7l3finr175dajlby-hello-2.10` were
already in the build inputs or built by a previous recursive Nix
call.
