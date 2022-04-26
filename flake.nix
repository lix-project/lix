{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        inherit (pkgs) stdenv;
        drvArgs = { srcDir = self; };
      in
      rec {
        packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;

        checks =
          let
            mkVariant = nix: (packages.nix-eval-jobs.override {
              inherit nix;
            }).overrideAttrs (_: {
              name = "nix-eval-jobs-${nix.version}";
              inherit (nix) version;
            });
          in
          {

            treefmt =
              let
                devShell = devShells.default;
              in
              stdenv.mkDerivation {
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

            build = mkVariant pkgs.nix;
            build-unstable = mkVariant pkgs.nixUnstable;
          };

        packages.default = self.packages.${system}.nix-eval-jobs;
        devShells.default = pkgs.callPackage ./shell.nix drvArgs;

      }
    );
}
