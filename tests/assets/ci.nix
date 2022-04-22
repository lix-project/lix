{ pkgs ? import (builtins.getFlake (toString ./.)).inputs.nixpkgs { } }:

{
  builtJob = pkgs.writeText "job1" "job1";
  substitutedJob = pkgs.hello;
  nested = {
    job = pkgs.hello;
  };
}
