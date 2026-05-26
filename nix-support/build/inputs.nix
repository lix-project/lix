{
  lib,
  lixSrc,
  nixpkgs,
  nix_2_18,
  nix2container,
  nixpkgs-regression,
}:

let
  sc = lib.makeScope (extra: lib.callPackageWith (sc // extra)) (self: {
    inherit lib;

    # Set to true to build the release notes for the next release.
    buildUnreleasedNotes = true;

    versionJson = builtins.fromJSON (builtins.readFile ../../version.json);
    officialRelease = self.versionJson.official_release;

    versionSuffix =
      if self.officialRelease then
        ""
      else
        "pre${
          builtins.substring 0 8 (lixSrc.lastModifiedDate or lixSrc.lastModified or "19700101")
        }-dev_${lixSrc.shortRev or "dirty"}";

    linux32BitSystems = [ "i686-linux" ];
    linux64BitSystems = [
      "x86_64-linux"
      "aarch64-linux"
    ];
    linuxSystems = self.linux32BitSystems ++ self.linux64BitSystems;
    darwinSystems = [
      "x86_64-darwin"
      "aarch64-darwin"
    ];
    nonDarwinSystems = self.linuxSystems;
    systems = self.linuxSystems ++ self.darwinSystems;

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

    forAllSystems = lib.genAttrs self.systems;
    # Same as forAllSystems, but removes nulls, in case something is broken
    # on that system.
    forAvailableSystems =
      f: lib.filterAttrs (name: value: value != null && value != { }) (self.forAllSystems f);

    forAllCrossSystems = lib.genAttrs self.crossSystems;

    forAllStdenvs =
      f:
      lib.listToAttrs (
        map (stdenvName: {
          name = "${stdenvName}Packages";
          value = f stdenvName;
        }) self.stdenvs
      )
      // {
        # TODO delete this and reënable gcc stdenvs once gcc compiles kj coros correctly
        stdenvPackages = f "clangStdenv";
      };

    # Memoize nixpkgs for different platforms for efficiency.
    nixpkgsFor = self.forAllSystems (
      system:
      let
        make-pkgs =
          crossSystem: stdenv:
          import nixpkgs {
            localSystem = {
              inherit system;
            };
            crossSystem = if crossSystem == null then null else { system = crossSystem; };
            overlays = [ (self.overlayFor (p: p.${stdenv})) ];
          };
        stdenvs = self.forAllStdenvs (make-pkgs null);
        native = stdenvs.stdenvPackages;
      in
      {
        inherit stdenvs native;
        static = native.pkgsStatic;
        cross = self.forAllCrossSystems (crossSystem: make-pkgs crossSystem "clangStdenv");
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
          # Nix 2.18 has been removed from Nixpkgs ≥ 25.05, so we need to reintroduce it ourselves for our tests.
          nix_2_18 =
            (import nix_2_18).packages.${currentStdenv.hostPlatform.system}.default.overrideAttrs
              (_: {
                pname = "nix";
              });
        };

        check-headers = final.buildPackages.callPackage ../../maintainers/check-headers.nix { };
        check-syscalls = final.buildPackages.callPackage ../../maintainers/check-syscalls.nix { };

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

        nix = final.callPackage ../../package.nix {
          inherit (self) versionSuffix officialRelease;
          stdenv = currentStdenv;
          busybox-sandbox-shell = final.busybox-sandbox-shell or final.default-busybox-sandbox-shell;
          # See below
          lowdown = final.lowdown_3_0;
          lowdown-unsandboxed = final.lowdown_3_0.override { enableDarwinSandbox = false; };
        };

        lix-clang-tidy = final.callPackage ../../subprojects/lix-clang-tidy { };

        nix-eval-jobs = final.callPackage ../../subprojects/nix-eval-jobs {
          stdenv = currentStdenv;
          srcDir = ../../subprojects/nix-eval-jobs;
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

        # As soon as Nixpkgs updates to >= 3.0.0, change to lowdown_2_0!
        # We don't change the default version in order to not change the hash
        # of Nix/Lix from upstream Nixpkgs.
        lowdown_3_0 =
          if (lib.versions.major prev.lowdown.version == "3") then
            prev.lowdown
          else
            prev.lowdown.overrideAttrs (
              finalAttrs: _prevAttrs: {
                version = "3.0.0";

                src = final.fetchurl {
                  url = "https://kristaps.bsd.lv/lowdown/snapshots/lowdown-${finalAttrs.version}.tar.gz";
                  sha512 = "94e97234d598382c3c3dc27f9bfdb3a3a2fcf7dbb6a8df3c85ee09f27f792449034a41d49d9cfd3d8450d2de01b8562c20c3d120e65c81af4d7d6c9454119e93";
                };
              }
            );

        capnproto = prev.capnproto.overrideAttrs (old: {
          patches =
            old.patches or [ ]
            ++ [
              # backport of https://github.com/capnproto/capnproto/pull/1810
              ../../misc/capnproto-promise-nodiscard.patch
            ]
            ++ lib.optionals (lib.versionOlder old.version "1.2.0") [
              # backport of https://github.com/capnproto/capnproto/pull/2296
              ../../misc/capnproto-monotonic-clocks-are-a-lie.patch
            ];
        });
      };

    inherit nix2container nixpkgs nixpkgs-regression;
  });
in
sc
