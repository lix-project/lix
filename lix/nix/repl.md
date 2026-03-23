R""(

**Note:** this command's interface is based heavily around [*installables*](./nix.md#installables), which you may want to read about first (`nix --help`).

# Examples

* Display all special commands within the REPL:

  ```console
  # nix repl
  nix-repl> :?
  ```

* Evaluate some simple Nix expressions:

  ```console
  # nix repl

  nix-repl> 1 + 2
  3

  nix-repl> map (x: x * 2) [1 2 3]
  [ 2 4 6 ]
  ```

* Interact with Nixpkgs in the REPL:

  ```console
  # nix repl --file example.nix
  Loading Installable ''...
  Added 3 variables.

  # nix repl --expr '{a={b=3;c=4;};}'
  Loading Installable ''...
  Added 1 variables.

  # nix repl --expr '{a={b=3;c=4;};}' a
  Loading Installable ''...
  Added 1 variables.

  # nix repl --extra-experimental-features 'flakes' nixpkgs
  Loading Installable 'flake:nixpkgs#'...
  Added 5 variables.

  nix-repl> legacyPackages.x86_64-linux.emacs.name
  "emacs-27.1"

  nix-repl> legacyPackages.x86_64-linux.emacs.name
  "emacs-27.1"

  nix-repl> :q

  # nix repl --expr 'import <nixpkgs>{}'

  Loading Installable ''...
  Added 12439 variables.

  nix-repl> emacs.name
  "emacs-27.1"

  nix-repl> emacs.drvPath
  "/nix/store/lp0sjrhgg03y2n0l10n70rg0k7hhyz0l-emacs-27.1.drv"

  nix-repl> drv = runCommand "hello" { buildInputs = [ hello ]; } "hello; hello > $out"

  nix-repl> :b drv
  this derivation produced the following outputs:
    out -> /nix/store/0njwbgwmkwls0w5dv9mpc1pq5fj39q0l-hello

  nix-repl> builtins.readFile drv
  "Hello, world!\n"

  nix-repl> :log drv
  Hello, world!
  ```

# Description

This command provides an interactive environment for evaluating Nix
expressions. (REPL stands for 'read–eval–print loop'.)

On startup, it loads the Nix expressions named *files* and adds them
into the lexical scope. You can load addition files using the `:l
<filename>` command, or reload all files using `:r`.

# Adding default variables in REPL sessions

It is possible to automatically load variables from a list of files
into each new REPL session using the
[`repl-overlays`](@docroot@/command-ref/conf-file.html#conf-repl-overlays)
configuration option.

Each file should contain a Nix function taking three taking and
returning an
[attribute set](@docroot@/language/values.html#attribute-set).
These three arguments are:

1. An [attribute set](@docroot@/language/values.html#attribute-set)
    containing at least a `currentSystem` attribute (this is identical
    to
    [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem),
    except that it's available in
    [`pure-eval`](@docroot@/command-ref/conf-file.html#conf-pure-eval)
    mode).
2. The top-level bindings produced by the previous `repl-overlays`
    value (or the default top-level bindings).
3. The final top-level bindings produced by calling all
    `repl-overlays`.

## Examples

* Aliasing `legacyPackages.${currentSystem}` to `pkgs`

  A file named `/home/alice/my-overlays.nix` containing the following code
  would, if `legacyPackages` exists, add a variable named `pkgs` into
  the global REPL scope, which returns the value of
  `legacyPackages.${currentSystem}`.

  ```nix
  info: final: prev:
  if prev ? legacyPackages
    && prev.legacyPackages ? ${info.currentSystem}
  then
  {
    pkgs = prev.legacyPackages.${info.currentSystem};
  }
  else
  { }
  ```

  This file can be loaded automatically for every REPL session by
  adding it to the value of
  [`repl-overlays`](@docroot@/command-ref/conf-file.html#conf-repl-overlays)
  inside `nix.conf`. For a single session, it is possible to add it
  using `--option repl-overlays /home/alice/my-overlay.nix`:

  ```console
  # nix repl --option repl-overlays /home/alice/my-overlay.nix nixpkgs

  nix-repl> pkgs == legacyPackages.${builtins.currentSystem}
  true

  nix-repl> pkgs.hello
  «derivation /nix/store/qdzln99hynf92vrz8sz91hlf1dmb1vdy-hello-2.12.2.drv»
  ```

See the `nix.conf`
[`repl-overlays`](@docroot@/command-ref/conf-file.html#conf-repl-overlays)
documentation for more information.

)""
