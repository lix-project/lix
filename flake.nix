{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/master";
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";
  inputs.treefmt-nix.url = "github:numtide/treefmt-nix";
  inputs.treefmt-nix.inputs.nixpkgs.follows = "nixpkgs";

  nixConfig.extra-substituters = [
    "https://cache.garnix.io"
  ];
  nixConfig.extra-trusted-public-keys = [
    "cache.garnix.io:CTFPyKSLcx5RMJKfLo5EEPUObbA78b0YQ2DTCJXqr9g="
  ];

  outputs = inputs @ { flake-parts, ... }:
    let
      inherit (inputs.nixpkgs) lib;
      inherit (inputs) self;
      nixVersion = lib.fileContents ./.nix-version;
    in
    flake-parts.lib.mkFlake { inherit inputs; }
      {
        systems = inputs.nixpkgs.lib.systems.flakeExposed;
        imports = [ inputs.treefmt-nix.flakeModule ];
        perSystem = { pkgs, self', ... }:
          let
            devShell = self'.devShells.default;
            drvArgs = {
              srcDir = self;
              nix = if nixVersion == "unstable" then pkgs.nixUnstable else pkgs.nixVersions."nix_${nixVersion}";
            };
          in
          {
            treefmt.imports = [ ./dev/treefmt.nix ];
            packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;
            packages.clangStdenv-nix-eval-jobs = pkgs.callPackage ./default.nix (drvArgs // { stdenv = pkgs.clangStdenv; });
            packages.default = self'.packages.nix-eval-jobs;
            devShells.default = pkgs.callPackage ./shell.nix drvArgs;
          };
      };
}
