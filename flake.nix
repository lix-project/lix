{
  description = "Lix: A modern, delicious implementation of the Nix package manager";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11-small";
    nixpkgs-regression.url = "github:NixOS/nixpkgs/215d4d0fd80ca5163643b03a33fde804a29cc1e2";

    # Required because Nix 2.18 is not in Nixpkgs ≥ 25.05 anymore.
    nix_2_18 = {
      url = "github:NixOS/nix/2.18.9";
      # NOTE(Raito): this is not possible because patches on libseccomp does not apply anymore on this Nix.
      # Let's keep the latest known nixpkgs useable with Nix 2.18 for our tests.
      # inputs.nixpkgs.follows = "nixpkgs";
      inputs.nixpkgs-regression.follows = "nixpkgs-regression";
      inputs.flake-compat.follows = "flake-compat";
    };

    pre-commit-hooks = {
      url = "github:cachix/git-hooks.nix";
      flake = false;
    };
    nix2container = {
      url = "github:nlewo/nix2container";
      flake = false;
    };
    flake-compat = {
      url = "https://git.lix.systems/lix-project/flake-compat/archive/main.tar.gz";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      nixpkgs-regression,
      pre-commit-hooks,
      nix2container,
      nix_2_18,
      flake-compat,
    }:

    let
      inherit (nixpkgs) lib;
      lixSrc = self;

      # This notice gets echoed as a dev shell hook, and can be turned off with
      # `touch .nocontribmsg`
      sgr = builtins.fromJSON ''"\u001b["'';
      freezePage = "https://wiki.lix.systems/books/lix-contributors/page/freezes-and-recommended-contributions";
      codebaseOverview = "https://wiki.lix.systems/books/lix-contributors/page/codebase-overview";
      gerritWiki = "https://wiki.lix.systems/books/lix-contributors/page/gerrit";
      contribNotice = builtins.toFile "lix-contrib-notice" ''
        Hey there!

        If you're thinking of working on Lix, please consider talking to us about it!
        You should be aware that we are ${sgr}1mnot${sgr}0m accepting major features without some conditions,
        and we highly recommend looking at our freeze status page on the wiki:
          ${sgr}32m${freezePage}${sgr}0m

        We also have an overview of the codebase at
          ${sgr}32m${codebaseOverview}${sgr}0m,
        and other helpful information on the wiki.

        But above all else, ${sgr}1mwe want to hear from you!${sgr}0m
        We can help you figure out where in the codebase to look for whatever you want to do,
        and we'd like to work together with all contributors as much as possible.
        Lix is a collaborative project :)

        If you want to submit a patch and you never used gerrit before, please
        check our gerrit wiki section:
          ${sgr}32m${gerritWiki}${sgr}0m

        You can open an issue at https://git.lix.systems/lix-project/lix/issues
        or chat with us on Matrix: #space:lix.systems.

        (Run `touch .nocontribmsg` to hide this message.)
      '';

      scope = import ./nix-support/build/inputs.nix {
        inherit
          lib
          nixpkgs
          nix_2_18
          nix2container
          lixSrc
          nixpkgs-regression
          ;
      };

      inherit (scope)
        crossSystems
        darwinSystems
        forAllStdenvs
        forAllSystems
        forAvailableSystems
        linux64BitSystems
        nixpkgsFor
        overlayFor
        systems
        versionSuffix
        ;

      inherit (scope.callPackage ./nix-support/build/outputs.nix { })
        packages
        ciArtifacts
        tests
        ;
    in
    {
      # for repl debugging
      inherit self;

      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlays.default = overlayFor (p: p.clangStdenv);

      hydraJobs = ciArtifacts // {
        # Aggregate job that is finished in Hydra _after_ all constituent jobs (here: grouped by system)
        # succeed.
        # This is used to run CD scripts once all builds are finished on Hydra.
        release = forAllSystems (
          system:
          let
            pkgs = nixpkgsFor.${system}.native;
          in
          pkgs.runCommand "release"
            {
              _hydraAggregate = true;
              constituents = lib.filter (x: x != null) (
                lib.mapAttrsToListRecursiveCond
                  (_: val: !(lib.isDerivation val || builtins.any (system': val ? ${system'}) systems))
                  (
                    path: drv:
                    if drv ? ${system} then
                      lib.concatStringsSep "." (path ++ [ system ])
                    else if drv.system or null == system then
                      lib.concatStringsSep "." path
                    else
                      null
                  )
                  (
                    removeAttrs self.hydraJobs [
                      "devShell"
                      "release"
                      "rl-next"
                    ]
                  )
              );
            }
            ''
              touch $out
            ''
        );

        devShell = forAllSystems (system: {
          default = self.devShells.${system}.default;
          clang = self.devShells.${system}.native-clangStdenvPackages;
        });

        inherit tests;

        pre-commit = forAvailableSystems (
          system:
          let
            pkgs = nixpkgsFor.${system}.native;
            pre-commit-check = import ./misc/pre-commit.nix { inherit self pkgs pre-commit-hooks; };
            # dotnet-sdk_6, a nativeBuildInputs of pre-commit, is broken on i686-linux.
            available = lib.meta.availableOn { inherit system; } pkgs.dotnet-sdk_6;
          in
          lib.optionalAttrs available pre-commit-check
        );
      };

      release-jobs = import ./releng/release-jobs.nix {
        inherit (self) hydraJobs;
        pkgs = nixpkgsFor.x86_64-linux.native;
      };

      releaseTests = lib.foldl lib.recursiveUpdate { } [
        (lib.genAttrs (linux64BitSystems ++ darwinSystems) (system: {
          nativeBuild = self.packages.${system}.nix;
        }))
        (lib.genAttrs (linux64BitSystems) (system: {
          staticBuild = self.packages.${system}.nix-static;
        }))
        {
          x86_64-linux = {
            # TODO add more cross/static release targets?
            crossBuild.aarch64-linux = self.packages.x86_64-linux.nix-aarch64-linux;

            # TODO wire up a nixos installer test with that lix and
            # run it, once nixpkgs can actually do that (again). :/
            # # nix build .#nixosTests.installer.{btrfsSimple,luksroot,lvm,simple,switchToFlake}
          };
        }
      ];

      # NOTE *do not* add fresh derivations to checks, always add them to
      # hydraJobs first (so CI will pick them up) and only link them here
      checks = forAvailableSystems (
        system:
        {
          # devShells and packages already get checked by nix flake check, so
          # this is just jobs that are special

          binaryTarball = self.hydraJobs.binaryTarball.${system};
          perlBindings = self.hydraJobs.perlBindings.${system};
          nix-eval-jobs = self.hydraJobs.nix-eval-jobs.${system};
          nixpkgsLibTests = self.hydraJobs.tests.nixpkgsLibTests.${system};
          rl-next = self.hydraJobs.rl-next.${system}.user;
          # Will be empty attr set on i686-linux, and filtered out by forAvailableSystems.
          pre-commit = self.hydraJobs.pre-commit.${system};
          repl-completion = self.hydraJobs.repl-completion.${system};
        }
        // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
          dockerImage = self.hydraJobs.dockerImage.${system};
        }
      );

      inherit packages;

      devShells =
        let
          makeShell =
            pkgs: stdenv:
            let
              nix = pkgs.callPackage ./package.nix {
                inherit stdenv versionSuffix;
                busybox-sandbox-shell = pkgs.busybox-sandbox-shell or pkgs.default-busybox-sandbox;
                internalApiDocs = false;
                includeSanitizerLibs = true;
                # Use LLD in the dev shell by default for faster link times.
                useLld = stdenv.hostPlatform.isLinux;
              };
              pre-commit = self.hydraJobs.pre-commit.${pkgs.stdenv.hostPlatform.system} or { };
            in
            pkgs.callPackage nix.mkDevShell {
              pre-commit-checks = pre-commit;
              inherit contribNotice;
            };
        in
        forAllSystems (
          system:
          let
            makeShells =
              prefix: pkgs:
              lib.mapAttrs' (k: v: lib.nameValuePair "${prefix}-${k}" v) (
                forAllStdenvs (stdenvName: makeShell pkgs pkgs.${stdenvName})
              );
          in
          (makeShells "native" nixpkgsFor.${system}.native)
          // (makeShells "static" nixpkgsFor.${system}.static)
          // (lib.listToAttrs (
            # Provide e.g., both '.#native-aarch64-linux` and `.#static-aarch64-linux`,
            # for each cross-system.
            # "native" feels like a misnomer here since it's literally cross compiling,
            # but at least it's consistent with the native/static dichotomy we've set up.
            lib.concatMap (
              crossSystem:
              let
                pkgs = nixpkgsFor.${system}.cross.${crossSystem};
                inherit (pkgs) pkgsStatic;
                native = makeShell pkgs pkgs.clangStdenv;
                static = makeShell pkgsStatic pkgsStatic.clangStdenv;
              in
              [
                {
                  name = "native-${crossSystem}";
                  value = native;
                }
                {
                  name = "static-${crossSystem}";
                  value = static;
                }
              ]
            ) crossSystems
          ))
          // {
            default = self.devShells.${system}.native-clangStdenvPackages;
          }
        );
    };
}
