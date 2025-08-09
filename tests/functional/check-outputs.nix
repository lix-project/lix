with import ./config.nix;

rec {
  dep = import ./dependencies.nix {};

  cycle = mkDerivation {
    name = "cycle";
    inherit dep;
    outputs = [ "foo" "bar" "baz" ];
    builder = builtins.toFile "builder.sh" ''
      mkdir -p $foo/bin $bar/lib $baz/share
      echo $bar > $foo/bin/alarm
      echo $baz > $bar/lib/libfoo
      echo $foo > $baz/share/lalala
    '';
  };

  as_dependency = mkDerivation {
    name = "depends-on-cycle";
    inherit cycle;
    builder = builtins.toFile "builder.sh" ''
      touch $out
    '';
  };
}
