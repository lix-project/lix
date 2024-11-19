---
name: toFile
args: [name, s]
---
Store the string *s* in a file in the Nix store and return its
path.  The file has suffix *name*. This file can be used as an
input to derivations. One application is to write builders
“inline”. For instance, the following Nix expression combines the
Nix expression for GNU Hello and its build script into one file:

```nix
{ stdenv, fetchurl, perl }:

stdenv.mkDerivation {
  name = "hello-2.1.1";

  builder = builtins.toFile "builder.sh" "
    source $stdenv/setup

    PATH=$perl/bin:$PATH

    tar xvfz $src
    cd hello-*
    ./configure --prefix=$out
    make
    make install
  ";

  src = fetchurl {
    url = "http://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz";
    sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
  };
  inherit perl;
}
```

It is even possible for one file to refer to another, e.g.,

```nix
builder = let
  configFile = builtins.toFile "foo.conf" "
    # This is some dummy configuration file.
    ...
  ";
in builtins.toFile "builder.sh" "
  source $stdenv/setup
  ...
  cp ${configFile} $out/etc/foo.conf
";
```

Note that `${configFile}` is a
[string interpolation](@docroot@/language/values.md#type-string), so the result of the
expression `configFile`
(i.e., a path like `/nix/store/m7p7jfny445k...-foo.conf`) will be
spliced into the resulting string.

It is however *not* allowed to have files mutually referring to each
other, like so:

```nix
let
  foo = builtins.toFile "foo" "...${bar}...";
  bar = builtins.toFile "bar" "...${foo}...";
in foo
```

This is not allowed because it would cause a cyclic dependency in
the computation of the cryptographic hashes for `foo` and `bar`.

It is also not possible to reference the result of a derivation. If
you are using Nixpkgs, the `writeTextFile` function is able to do
that.
