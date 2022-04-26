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
              # TODO: fix to stable after next nix release
              nix = pkgs.nix;
              #inherit nix;
            }).overrideAttrs (_: {
              name = "nix-eval-jobs-${nix.version}";
              inherit (nix) version;
            });
          in
          {

            treefmt = stdenv.mkDerivation {
              name = "treefmt-check";
              src = self;
              nativeBuildInputs = devShells.default.nativeBuildInputs;
              dontConfigure = true;

              buildPhase = ''
                env HOME=$(mktemp -d) treefmt --fail-on-change
              '';

              installPhase = "touch $out";
            };

            build = mkVariant pkgs.nix;
            build-unstable = mkVariant pkgs.nixUnstable;
          };

        defaultPackage = self.packages.${system}.nix-eval-jobs;
        devShell = pkgs.callPackage ./shell.nix drvArgs;

      }
    );
}
