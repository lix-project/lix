{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        drvArgs = { srcDir = self; };
      in
      rec {
        packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;

        checks = {

          editorconfig = pkgs.runCommand "editorconfig-checks" {
            nativeBuildInputs = [
              pkgs.editorconfig-checker
            ];
          } ''
            editorconfig-checker ${self}
            touch $out
          '';

          build = packages.nix-eval-jobs;

        };

        defaultPackage = self.packages.${system}.nix-eval-jobs;
        devShell = pkgs.callPackage ./shell.nix drvArgs;

      });
}
