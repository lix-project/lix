R""(

**Note:** this command's interface is based heavily around [*installables*](./nix.md#installables), which you may want to read about first (`nix --help`).

# Examples

* Evaluate a Nix expression given on the command line:

  ```console
  # nix eval --expr '1 + 2'
  ```

* Evaluate a Nix expression to JSON using the short-form expression flag:

  ```console
  # nix eval --json -E '{ x = 1; }'
  {"x":1}
  ```

* Evaluate a Nix expression from a file:

  ```console
  # nix eval --file ./my-nixpkgs hello.name
  ```

* Get the current version of the `nixpkgs` flake:

  ```console
  # nix eval --raw nixpkgs#lib.version
  ```

* Print the store path of the Hello package:

  ```console
  # nix eval --raw nixpkgs#hello
  ```

* Get a list of checks in the `nix` flake:

  ```console
  # nix eval nix#checks.x86_64-linux --apply builtins.attrNames
  ```

# Description

This command evaluates the given Nix expression and prints the
result on standard output.

# Output format

`nix eval` can produce output in several formats:

* By default, the evaluation result is printed as a Nix expression.

* With `--json`, the evaluation result is printed in JSON format.

  The conversion behaviour is the same as
  [`builtins.toJSON`](../../language/builtins.md#builtins-toJSON) except that
  paths are printed as-is without being copied to the Nix store.

* With `--raw`, the result must be coercible to a string, i.e.,
  something that can be converted using `${...}`.

  Integers will always generate an error when output via `--raw`, regardless of
  [`coerce-integers`](../../contributing/experimental-features.md#xp-feature-coerce-integers) being enabled, to avoid ambiguity.

  The output is printed exactly as-is, with no quotes, escaping, or trailing
  newline.

)""
