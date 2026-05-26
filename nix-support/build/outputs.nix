{
  forAllSystems,
  nixpkgsFor,
  linux64BitSystems,
  crossSystems,
  stdenvs,
  lib,
  nix2container,
  nixpkgs,
  nonDarwinSystems,
  nixpkgs-regression,
}:

lib.fix (self: {
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

  # System tests.
  tests =
    import ../../tests/nixos {
      inherit
        lib
        nixpkgs
        nixpkgsFor
        ;
    }
    // {
      # the n-e-j test suite is unusably slow in darwin ci. disbled until anywho fixes this.
      nix-eval-jobs = (lib.genAttrs nonDarwinSystems) (
        system: self.packages.${system}.nix-eval-jobs.tests.nix-eval-jobs
      );

      # This is x86_64-linux only, just because we have significantly
      # cheaper x86_64-linux compute in CI.
      # It is clangStdenv because clang's sanitizers are nicer.
      asanBuild = self.packages.x86_64-linux.nix-clangStdenv.override {
        # Improve caching of non-code changes by not changing the
        # derivation name every single time, since this will never be seen
        # by users anyway.
        versionSuffix = "";
        sanitize = [
          "address"
          "undefined"
        ];
        # it is very hard to make *every* CI build use this option such
        # that we don't wind up building Lix twice, so we do it here where
        # we are already doing so.
        werror = true;
      };

      # Although this might be nicer to do with pre-commit, that would
      # require adding 12MB of nodejs to the dev shell, whereas building it
      # in CI with Nix avoids that at a cost of slower feedback on rarely
      # touched files.
      jsSyntaxCheck =
        let
          nixpkgs = nixpkgsFor.x86_64-linux.native;
          inherit (nixpkgs) pkgs;
          docSources = lib.fileset.toSource {
            root = ../../doc;
            fileset = lib.fileset.fileFilter (f: f.hasExt "js") ../../doc;
          };
        in
        pkgs.runCommand "js-syntax-check" { } ''
          find ${docSources} -type f -print -exec ${pkgs.nodejs-slim}/bin/node --check '{}' ';'
          touch $out
        '';

      # clang-tidy run against the Lix codebase using the Lix clang-tidy plugin
      clang-tidy = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system}.native;
        in
        pkgs.callPackage ../../package.nix {
          # Required since we don't support gcc stdenv
          stdenv = pkgs.clangStdenv;
          versionSuffix = "";
          lintInsteadOfBuild = true;
        }
      );

      # Make sure that nix-env still produces the exact same result
      # on a particular version of Nixpkgs.
      evalNixpkgs = nixpkgsFor.x86_64-linux.native.callPackage ../../tests/nixpkgs/eval.nix {
        inherit nixpkgs-regression;
      };

      nixpkgsLibTests = forAllSystems (
        system:
        nixpkgsFor.${system}.native.callPackage ../../tests/nixpkgs/lib.nix {
          inherit nixpkgs system;
          inherit (self.packages.${system}) nix;
        }
      );
    };
})
