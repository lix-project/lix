{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      hydraJobs = import ./ci.nix { inherit pkgs; };

      legacyPackages.x86_64-linux = {
        brokenPkgs = {
          brokenPackage = throw "this is an evaluation error";
        };
        infiniteRecursionPkgs = {
          packageWithInfiniteRecursion =
            let
              recursion = [ recursion ];
            in
            derivation {
              inherit (pkgs) system;
              name = "drvB";
              recursiveAttr = recursion;
              builder = ":";
            };
        };
      };
    };
}
