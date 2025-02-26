{
  pkgs ? import (builtins.getFlake (toString ./.)).inputs.nixpkgs { },
  system ? pkgs.stdenv.hostPlatform.system,
}:

{
  builtJob = pkgs.writeText "job1" "job1";
  substitutedJob = pkgs.hello;

  dontRecurse = {
    # This shouldn't build as `recurseForDerivations = true;` is not set
    # recurseForDerivations = true;

    # This should not build
    drvB = derivation {
      inherit system;
      name = "drvA";
      builder = ":";
    };
  };

  recurse = {
    # This should build
    recurseForDerivations = true;

    # This should not build
    drvB = derivation {
      inherit system;
      name = "drvB";
      builder = ":";
    };
  };

  "dotted.attr" = pkgs.hello;

}
