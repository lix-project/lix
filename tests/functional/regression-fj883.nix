with import ./config.nix;

rec {
  base = mkDerivation {
    name = "base";
    outputs = [ "out" "lib" ];
    buildCommand = "echo > $out; echo > $lib";
  };

  downstream = mkDerivation {
    name = "downstream";
    deps = [ base.out base.lib ];
    buildCommand = "echo $deps > $out";
  };
}
