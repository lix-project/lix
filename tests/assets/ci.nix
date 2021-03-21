with import <nixpkgs> {};
{
  builtJob = pkgs.writeText "job1" "job1";
  substitutedJob = pkgs.hello;
}
