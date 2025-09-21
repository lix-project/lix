{
  description = "Lix: A modern, delicious implementation of the Nix package manager";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05-small";
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
      url = "github:edolstra/flake-compat";
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

      versionJson = builtins.fromJSON (builtins.readFile ./version.json);
      officialRelease = versionJson.official_release;

      # Set to true to build the release notes for the next release.
      buildUnreleasedNotes = true;

      versionSuffix =
        if officialRelease then
          ""
        else
          "pre${
            builtins.substring 0 8 (self.lastModifiedDate or self.lastModified or "19700101")
          }_${self.shortRev or "dirty"}";

      linux32BitSystems = [ "i686-linux" ];
      linux64BitSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      linuxSystems = linux32BitSystems ++ linux64BitSystems;
      darwinSystems = [
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      systems = linuxSystems ++ darwinSystems;

      # If you add something here, please update the list in doc/manual/src/contributing/hacking.md.
      # Thanks~
      crossSystems = [
        "armv6l-linux"
        "armv7l-linux"
        "riscv64-linux"
        "aarch64-linux"
        "x86_64-freebsd"
        # FIXME: broken dev shell due to python
        # "x86_64-netbsd"
      ];

      stdenvs = [
        # see assertion in package.nix why these two are disabled
        # "stdenv"
        # "gccStdenv"
        "clangStdenv"
        "libcxxStdenv"
        "ccacheStdenv"
      ];

      forAllSystems = lib.genAttrs systems;
      # Same as forAllSystems, but removes nulls, in case something is broken
      # on that system.
      forAvailableSystems =
        f: lib.filterAttrs (name: value: value != null && value != { }) (forAllSystems f);

      forAllCrossSystems = lib.genAttrs crossSystems;

      forAllStdenvs =
        f:
        lib.listToAttrs (
          map (stdenvName: {
            name = "${stdenvName}Packages";
            value = f stdenvName;
          }) stdenvs
        )
        // {
          # TODO delete this and reënable gcc stdenvs once gcc compiles kj coros correctly
          stdenvPackages = f "clangStdenv";
        };

      # Memoize nixpkgs for different platforms for efficiency.
      nixpkgsFor = forAllSystems (
        system:
        let
          make-pkgs =
            crossSystem: stdenv:
            import nixpkgs {
              localSystem = {
                inherit system;
              };
              crossSystem = if crossSystem == null then null else { system = crossSystem; };
              overlays = [ (overlayFor (p: p.${stdenv})) ];
            };
          stdenvs = forAllStdenvs (make-pkgs null);
          native = stdenvs.stdenvPackages;
        in
        {
          inherit stdenvs native;
          static = native.pkgsStatic;
          cross = forAllCrossSystems (crossSystem: make-pkgs crossSystem "clangStdenv");
        }
      );

      overlayFor =
        getStdenv: final: prev:
        let
          currentStdenv = getStdenv final;
        in
        {
          nixStable = prev.nix;

          nixVersions = prev.nixVersions // {
            nix_2_3 = prev.nixVersions.nix_2_3.overrideAttrs (old: {
              meta = old.meta // {
                knownVulnerabilities = [ ];
              };
            });
            # Nix 2.18 has been removed from Nixpkgs ≥ 25.05, so we need to reintroduce it ourselves for our tests.
            nix_2_18 = nix_2_18.outputs.packages.${currentStdenv.hostPlatform.system}.default;
          };

          # Forward from the previous stage as we don’t want it to pick the lowdown override
          nixUnstable = prev.nixUnstable;

          check-headers = final.buildPackages.callPackage ./maintainers/check-headers.nix { };
          check-syscalls = final.buildPackages.callPackage ./maintainers/check-syscalls.nix { };

          default-busybox-sandbox-shell = final.busybox.override {
            useMusl = true;
            enableStatic = true;
            enableMinimal = true;
            extraConfig = ''
              CONFIG_FEATURE_FANCY_ECHO y
              CONFIG_FEATURE_SH_MATH y
              CONFIG_FEATURE_SH_MATH_64 y

              CONFIG_ASH y
              CONFIG_ASH_OPTIMIZE_FOR_SIZE y

              CONFIG_ASH_ALIAS y
              CONFIG_ASH_BASH_COMPAT y
              CONFIG_ASH_CMDCMD y
              CONFIG_ASH_ECHO y
              CONFIG_ASH_GETOPTS y
              CONFIG_ASH_INTERNAL_GLOB y
              CONFIG_ASH_JOB_CONTROL y
              CONFIG_ASH_PRINTF y
              CONFIG_ASH_TEST y
            '';
          };

          nix = final.callPackage ./package.nix {
            inherit versionSuffix officialRelease;
            stdenv = currentStdenv;
            busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
          };

          lix-clang-tidy = final.callPackage ./subprojects/lix-clang-tidy { };

          nix-eval-jobs = final.callPackage ./subprojects/nix-eval-jobs {
            srcDir = ./subprojects/nix-eval-jobs;
          };

          # HACK: We need nix-prefetch-git for fetchCargoVendor for Rust stuff,
          # so it can't use Lix, or we infrec:
          #   lix -> Rust stuff -> fetchCargoVendor -> nix-prefetch-git -> nix (lix)
          # This will eventually become a problem upstream, but until then,
          # apply some duct tape and pray.
          nix-prefetch-git =
            if (lib.functionArgs prev.nix-prefetch-git.override) ? "nix" then
              prev.nix-prefetch-git.override { nix = prev.nix; }
            else
              prev.nix-prefetch-git;

          # Export the patched version of boehmgc that Lix uses into the overlay
          # for consumers of this flake.
          boehmgc-nix = final.nix.passthru.boehmgc-nix;
          # And same thing for our build-release-notes package.
          build-release-notes = final.nix.passthru.build-release-notes;

          lowdown_1_3 =
            # If the stable channel we are using ships lowdown >= 1.4, we need
            # to swap this around, take the default lowdown from the stable
            # channel and add an overridden one for the legacy version.
            assert lib.versionOlder prev.lowdown.version "1.4.0";
            prev.lowdown;
          lowdown = prev.lowdown.overrideAttrs (prevAttrs: rec {
            version = "2.0.2";
            src = final.fetchurl {
              url = "https://kristaps.bsd.lv/lowdown/snapshots/lowdown-${version}.tar.gz";
              sha512 = "2a4d0rqh8gkw4ca3gkzddp0hjpmmw74cbks8k0inhh0vizmgbn188zdv6m1kgmr019b99g7insli8js3ci1ji7y4n5nk704bswf3z3i";
            };
            nativeBuildInputs = prevAttrs.nativeBuildInputs ++ [ final.buildPackages.bmake ];
            postInstall = lib.replaceStrings [ "lowdown.so.1" ] [ "lowdown.so.2" ] prevAttrs.postInstall;
          });
        };
    in
    {
      # for repl debugging
      inherit self;

      # A Nixpkgs overlay that overrides the 'nix' and
      # 'nix.perl-bindings' packages.
      overlays.default = overlayFor (p: p.clangStdenv);

      hydraJobs = {
        # Binary package for various platforms.
        build = forAllSystems (system: self.packages.${system}.nix);

        # Building Lix twice in CI is expensive, but we can catch a lot of static
        # build regressions by at least making sure it evals and configures.
        configure-static = lib.genAttrs linux64BitSystems (
          system:
          self.packages.${system}.nix-static.overrideAttrs {
            dontBuild = true;
            installPhase = ''
              runHook preInstall

              echo "configure-static complete. exiting with success"
              mkdir -p "$out"
              exit 0
            '';
          }
        );

        # Ensure support for lowdown < 1.4 doesn't regress
        build-lowdown_1_3 = forAllSystems (
          system:
          self.packages.${system}.nix.override {
            lowdown = nixpkgsFor.${system}.native.lowdown_1_3;
          }
        );

        devShell = forAllSystems (system: {
          default = self.devShells.${system}.default;
          clang = self.devShells.${system}.native-clangStdenvPackages;
        });

        rl-next = forAllSystems (
          system:
          let
            rl-next-check =
              name: dir:
              let
                pkgs = nixpkgsFor.${system}.native;
              in
              pkgs.buildPackages.runCommand "test-${name}-release-notes" { } ''
                LANG=C.UTF-8 ${lib.getExe pkgs.build-release-notes} --change-authors ${./doc/manual/change-authors.yml} ${dir} >$out
              '';
          in
          {
            user = rl-next-check "rl-next" ./doc/manual/rl-next;
          }
        );

        # Completion tests for the Nix REPL.
        repl-completion = forAllSystems (
          system: nixpkgsFor.${system}.native.callPackage ./tests/repl-completion.nix { }
        );

        # Perl bindings for various platforms.
        perlBindings = forAllSystems (system: nixpkgsFor.${system}.native.nix.passthru.perl-bindings);

        # nix-eval-jobs can be built against this Lix.
        nix-eval-jobs = forAllSystems (system: nixpkgsFor.${system}.native.nix-eval-jobs);

        # Binary tarball for various platforms, containing a Nix store
        # with the closure of 'nix' package.
        binaryTarball = forAllSystems (system: nixpkgsFor.${system}.native.nix.passthru.binaryTarball);

        # docker image with Lix inside
        dockerImage = lib.genAttrs linux64BitSystems (system: self.packages.${system}.dockerImage);

        # API docs for Nix's unstable internal C++ interfaces.
        internal-api-docs =
          let
            nixpkgs = nixpkgsFor.x86_64-linux.native;
            inherit (nixpkgs) pkgs;

            nix = pkgs.callPackage ./package.nix {
              inherit versionSuffix officialRelease buildUnreleasedNotes;
              inherit (pkgs) build-release-notes;
              # Required since we don't support gcc stdenv
              stdenv = pkgs.clangStdenv;
              internalApiDocs = true;
              busybox-sandbox-shell = pkgs.busybox-sandbox-shell;
            };
          in
          nix.overrideAttrs (prev: {
            # This Hydra job is just for the internal API docs.
            # We don't need the build artifacts here.
            dontBuild = true;
            doCheck = false;
            doInstallCheck = false;
          });

        # System tests.
        tests =
          import ./tests/nixos {
            inherit
              self
              lib
              nixpkgs
              nixpkgsFor
              ;
          }
          // {
            nix-eval-jobs = forAllSystems (system: self.packages.${system}.nix-eval-jobs.tests.nix-eval-jobs);

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
                  root = ./doc;
                  fileset = lib.fileset.fileFilter (f: f.hasExt "js") ./doc;
                };
              in
              pkgs.runCommand "js-syntax-check" { } ''
                find ${docSources} -type f -print -exec ${pkgs.nodejs-slim}/bin/node --check '{}' ';'
                touch $out
              '';

            # clang-tidy run against the Lix codebase using the Lix clang-tidy plugin
            clang-tidy =
              let
                nixpkgs = nixpkgsFor.x86_64-linux.native;
                inherit (nixpkgs) pkgs;
              in
              pkgs.callPackage ./package.nix {
                # Required since we don't support gcc stdenv
                stdenv = pkgs.clangStdenv;
                versionSuffix = "";
                lintInsteadOfBuild = true;
              };

            # Make sure that nix-env still produces the exact same result
            # on a particular version of Nixpkgs.
            evalNixpkgs =
              with nixpkgsFor.x86_64-linux.native;
              runCommand "eval-nixos" { buildInputs = [ nix ]; } ''
                type -p nix-env
                # Note: we're filtering out nixos-install-tools because https://github.com/NixOS/nixpkgs/pull/153594#issuecomment-1020530593.
                time nix-env --store dummy:// -f ${nixpkgs-regression} -qaP --drv-path | sort | grep -v nixos-install-tools > packages
                [[ $(sha1sum < packages | cut -c1-40) = 402242fca90874112b34718b8199d844e8b03d12 ]]
                mkdir $out
              '';

            nixpkgsLibTests = forAllSystems (
              system:
              let
                inherit (self.packages.${system}) nix;
                pkgs = nixpkgsFor.${system}.native;
                testWithNix = import (nixpkgs + "/lib/tests/test-with-nix.nix") { inherit pkgs lib nix; };
              in
              pkgs.symlinkJoin {
                name = "nixpkgs-lib-tests";
                paths = [
                  testWithNix
                ]
                # NOTE: nixpkgs 25.05 is being ... *creative*, and requires this dance to override
                # the evaluator used for the test. it will break again in the future, don't worry.
                ++ lib.optionals pkgs.stdenv.isLinux [
                  ((pkgs.callPackage "${nixpkgs}/ci/eval" { inherit nix; }).attrpathsSuperset {
                    evalSystem = system;
                  })
                ];
              }
            );
          };

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

          build-lowdown_1_3 = self.hydraJobs.build-lowdown_1_3.${system};
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
                nix2container' = import nix2container { inherit pkgs system; };
              in
              import ./docker.nix {
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

      devShells =
        let
          makeShell =
            pkgs: stdenv:
            let
              nix = pkgs.callPackage ./package.nix {
                inherit stdenv versionSuffix;
                busybox-sandbox-shell = pkgs.busybox-sandbox-shell or pkgs.default-busybox-sandbox;
                internalApiDocs = false;
              };
              pre-commit = self.hydraJobs.pre-commit.${pkgs.system} or { };
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
          // (forAllCrossSystems (
            crossSystem:
            let
              pkgs = nixpkgsFor.${system}.cross.${crossSystem};
            in
            makeShell pkgs pkgs.clangStdenv
          ))
          // {
            default = self.devShells.${system}.native-clangStdenvPackages;
          }
        );
    };
}
