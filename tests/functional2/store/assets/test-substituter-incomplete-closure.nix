with import ./config.nix;
let
  dependency-will-fail-to-substitute = mkDerivation {
    name = "dependency-will-fail-to-substitute";
    buildCommand = "echo a > $out";
  };

  foo = mkDerivation {
    name = "foo";
    outputs = [
      "a"
      "b"
    ];
    buildCommand = ''
      echo ${dependency-will-fail-to-substitute} > $a
      echo b3 > $b
    '';
  };

  bar = mkDerivation {
    name = "bar";
    buildCommand = ''
      echo ${foo.b} >> $out
    '';
  };
in
{
  foo-a = foo.a;
  inherit bar;
}
