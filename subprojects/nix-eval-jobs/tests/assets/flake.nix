{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { self, nixpkgs, ... }:
    let

      pkgs = nixpkgs.legacyPackages.x86_64-linux;

      mkDrvWithConstituents =
        name: constituents:
        pkgs.runCommand name {
          _hydraAggregate = true;
          inherit constituents;
        } "touch $out";
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
              inherit (pkgs.stdenv.hostPlatform) system;
              name = "drvB";
              recursiveAttr = recursion;
              builder = ":";
            };
        };
        constituents = {
          success = {
            indirect_aggregate = mkDrvWithConstituents "indirect_aggregate" [
              "anotherone"
            ];
            direct_aggregate = mkDrvWithConstituents "direct_aggregate" [
              self.hydraJobs.builtJob
            ];
            mixed_aggregate = mkDrvWithConstituents "mixed_aggregate" [
              self.hydraJobs.builtJob
              "anotherone"
            ];
            anotherone = pkgs.writeText "constituent" "text";
          };
          failures = {
            aggregate = mkDrvWithConstituents "aggregate" [
              "doesntexist"
              "doesnteval"
            ];
            doesnteval = pkgs.writeText "constituent" (toString { });
          };
          cycle = {
            aggregate0 = mkDrvWithConstituents "aggregate0" [
              "aggregate1"
            ];
            aggregate1 = mkDrvWithConstituents "aggregate1" [
              "aggregate0"
            ];
          };
          transitive = {
            constituent = pkgs.hello;
            # also flip the order to make sure the toposort works as intended.
            aggregate1 = mkDrvWithConstituents "aggregate1" [ "constituent" ];
            aggregate0 = mkDrvWithConstituents "aggregate0" [ "aggregate1" ];
          };
        };
      };
    };
}
