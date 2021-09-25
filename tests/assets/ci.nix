let
  pkgs = import (builtins.getFlake (toString ./.)).inputs.nixpkgs { };
in
{
  builtJob = pkgs.writeText "job1" "job1";
  substitutedJob = pkgs.hello;
}
