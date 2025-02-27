{ filterin }:

with import ./config.nix;

mkDerivation {
  name = "filter";
  builder = builtins.toFile "builder" "ln -s $input $out";
  input =
    builtins.path {
      path = filterin;
      filter = path: type:
        type != "symlink"
        && (builtins.substring 0 (builtins.stringLength filterin) (builtins.toString path) == filterin)
        && baseNameOf path != "foo"
        && !((import ./lang/lib.nix).hasSuffix ".bak" (baseNameOf path));
    };
}
