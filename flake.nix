{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/master";
  inputs.flake-parts.url = "github:hercules-ci/flake-parts";
  inputs.flake-parts.inputs.nixpkgs-lib.follows = "nixpkgs";

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
        perSystem = { pkgs, self', ... }:
          let
            devShell = self'.devShells.default;
            drvArgs = {
              srcDir = self;
              nix = if nixVersion == "unstable" then pkgs.nixUnstable else pkgs.nixVersions."nix_${nixVersion}";
            };
          in
          {
            packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;

            checks.treefmt = pkgs.stdenv.mkDerivation {
              name = "treefmt-check";
              src = self;
              nativeBuildInputs = devShell.nativeBuildInputs;
              dontConfigure = true;

              inherit (devShell) NODE_PATH;

              buildPhase = ''
                env HOME=$(mktemp -d) treefmt --fail-on-change
              '';

              installPhase = "touch $out";
            };

            packages.default = self'.packages.nix-eval-jobs;
            devShells.default = pkgs.callPackage ./shell.nix drvArgs;
          };
      };
}
