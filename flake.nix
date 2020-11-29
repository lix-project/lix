{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: {
      packages.hydra-eval-jobs = nixpkgs.legacyPackages.${system}.callPackage ./. {
        srcDir = self;
      };
      defaultPackage = self.packages.${system}.hydra-eval-jobs;
    });
}
