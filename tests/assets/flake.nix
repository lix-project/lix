{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      hydraJobs = import ./ci.nix { inherit pkgs; };

      legacyPackages.x86_64-linux.brokenPackage = throw "this is an evaluation error";
    };
}
