with import ./config.nix;
rec {
  x1 = mkDerivation {
    name = "x1";
    builder = builtins.toFile "builder.sh"
      ''
        echo $name > $out
      '';
    url = "https://meow.puppy.forge/puppy.tar.gz";
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x2 = mkDerivation {
    name = "x2";
    builder = builtins.toFile "builder.sh"
      ''
        echo $name > $out
      '';
    urls = "https://kitty.forge/cat.tar.gz";
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x3 = mkDerivation {
    name = "x3";
    builder = builtins.toFile "builder.sh"
      ''
        echo $name > $out
      '';
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x4 = mkDerivation {
    name = "x4";
    inherit x2 x3;
    builder = builtins.toFile "builder.sh"
      ''
        echo $x2 $x3
        exit 1
      '';
  };
  x5 = mkDerivation {
    name = "x5";
    __structuredAttrs = true;
    buildCommand = "echo $name > \${outputs[out]}";
    url = "https://avian.example/owls.tar.gz";
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x6 = mkDerivation {
    name = "x6";
    __structuredAttrs = true;
    buildCommand = "echo $name > \${outputs[out]}";
    urls = [ "https://avian.example/geese.tar.gz" ];
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x7 = mkDerivation {
    name = "x7";
    __structuredAttrs = true;
    buildCommand = "echo $name > \${outputs[out]}";
    url = 42;
    urls = 0;
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
  x8 = mkDerivation {
    name = "x8";
    __structuredAttrs = true;
    buildCommand = "echo $name > \${outputs[out]}";
    urls = [ "https://avian.example/swan1.tar.gz" "https://avian.example/swan2.tar.gz" ];
    outputHashMode = "recursive";
    outputHash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
  };
}
