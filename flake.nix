{
  description = "Hydra's builtin hydra-eval-jobs as a standalone";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs =
    { self
    , nixpkgs
    , flake-utils
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        nixVersion = nixpkgs.lib.fileContents ./.nix-version;
        pkgs = nixpkgs.legacyPackages.${system};
        inherit (pkgs) stdenv;
        devShell = self.devShells.${system}.default;
        drvArgs = {
          srcDir = self;
          nix = if nixVersion == "unstable" then pkgs.nixUnstable else pkgs.nixVersions."nix_${nixVersion}";
        };
      in
      {
        packages.nix-eval-jobs = pkgs.callPackage ./default.nix drvArgs;

        checks.treefmt = stdenv.mkDerivation {
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

        packages.default = self.packages.${system}.nix-eval-jobs;
        devShells.default = pkgs.callPackage ./shell.nix drvArgs;
      }
    );
}
