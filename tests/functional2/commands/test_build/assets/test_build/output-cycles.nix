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

  cycle-with-deps = mkDerivation {
    name = "cycle-with-deps";
    inherit dep;
    outputs = [ "foo" "bar" ];
    builder = builtins.toFile "builder.sh" ''
      mkdir -p $foo/bin $bar/lib
      ln -sf $dep $bar/lib
      echo $foo > $bar/txt
      echo $bar > $foo/txt
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
