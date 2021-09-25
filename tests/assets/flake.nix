{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";

  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      hydraJobs = {
        builtJob = pkgs.writeText "job1" "job1";
        substitutedJob = pkgs.hello;
      };
    };
}
