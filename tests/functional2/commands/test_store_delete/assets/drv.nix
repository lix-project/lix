with import ./config.nix;

let
  dependency = mkDerivation {
    name = "dependency";
    builder = builtins.toFile "builder.sh"
    ''
      #!/usr/bin/env bash
      echo "beans" > "$out"
    '';
  };

in
mkDerivation {
  name = "some-package";
  builder = builtins.toFile "builder.sh"
  ''
    #!/usr/bin/env bash
    mkdir "$out"
    echo "$dependency" > "$out/toe-kind"
  '';
  inherit dependency;
}
