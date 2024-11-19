---
name: currentSystem
type: string
constructorArgs: [evalSettings.getCurrentSystem()]
impure: true
---
The value of the
[`eval-system`](@docroot@/command-ref/conf-file.md#conf-eval-system)
or else
[`system`](@docroot@/command-ref/conf-file.md#conf-system)
configuration option.

It can be used to set the `system` attribute for [`builtins.derivation`](@docroot@/language/derivations.md) such that the resulting derivation can be built on the same system that evaluates the Nix expression:

```nix
 builtins.derivation {
   # ...
   system = builtins.currentSystem;
}
```

It can be overridden in order to create derivations for different system than the current one:

```console
$ nix-instantiate --system "mips64-linux" --eval --expr 'builtins.currentSystem'
"mips64-linux"
```
