---
name: import
args: [path]
renameInGlobalScope: false
---
Load, parse and return the Nix expression in the file *path*.

The value *path* can be a path, a string, or an attribute set with an
`__toString` attribute or a `outPath` attribute (as derivations or flake
inputs typically have).

If *path* is a directory, the file `default.nix` in that directory
is loaded.

Evaluation aborts if the file doesn’t exist or contains
an incorrect Nix expression. `import` implements Nix’s module
system: you can put any Nix expression (such as a set or a
function) in a separate file, and use it from Nix expressions in
other files.

> **Note**
>
> Unlike some languages, `import` is a regular function in Nix.
> Paths using the angle bracket syntax (e.g., `import` *\<foo\>*)
> are normal [path values](@docroot@/language/values.md#type-path).

A Nix expression loaded by `import` must not contain any *free
variables* (identifiers that are not defined in the Nix expression
itself and are not built-in). Therefore, it cannot refer to
variables that are in scope at the call site. For instance, if you
have a calling expression

```nix
rec {
  x = 123;
  y = import ./foo.nix;
}
```

then the following `foo.nix` will give an error:

```nix
x + 456
```

since `x` is not in scope in `foo.nix`. If you want `x` to be
available in `foo.nix`, you should pass it as a function argument:

```nix
rec {
  x = 123;
  y = import ./foo.nix x;
}
```

and

```nix
x: x + 456
```

(The function argument doesn’t have to be called `x` in `foo.nix`;
any name would work.)
