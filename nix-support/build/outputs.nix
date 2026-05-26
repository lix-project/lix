{
  forAllSystems,
  nixpkgsFor,
  linux64BitSystems,
  crossSystems,
  stdenvs,
  lib,
  nix2container,
}:

{
  packages = forAllSystems (
    system:
    rec {
      inherit (nixpkgsFor.${system}.native) nix;
      default = nix;

      inherit (nixpkgsFor.${system}.native) lix-clang-tidy nix-eval-jobs;
    }
    // (
      lib.optionalAttrs (builtins.elem system linux64BitSystems) {
        # python doesn't work in static builds as of 2025-06-27
        nix-static = nixpkgsFor.${system}.static.nix.overrideAttrs (_: {
          doCheck = false;
        });
        dockerImage =
          let
            pkgs = nixpkgsFor.${system}.native;
            nix2container' = import nix2container { inherit pkgs; };
          in
          import ../../docker.nix {
            inherit pkgs;
            nix2container = nix2container'.nix2container;
            tag = pkgs.nix.version;
          };
      }
      // builtins.listToAttrs (
        map (crossSystem: {
          name = "nix-${crossSystem}";
          value = nixpkgsFor.${system}.cross.${crossSystem}.nix;
        }) crossSystems
      )
      // builtins.listToAttrs (
        map (stdenvName: {
          name = "nix-${stdenvName}";
          value = nixpkgsFor.${system}.stdenvs."${stdenvName}Packages".nix;
        }) stdenvs
      )
    )
  );
}
