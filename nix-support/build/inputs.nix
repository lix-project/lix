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

    versionSuffix = if self.officialRelease then "" else "${lixSrc.shortRev or "dirty"}";

    linux32BitSystems = [ "i686-linux" ];
    linux64BitSystems = [
      "x86_64-linux"
      "aarch64-linux"
    ];
    linuxSystems = self.linux32BitSystems ++ self.linux64BitSystems;
    darwinSystems = [
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

    overlayFor = self.callPackage ./overlay.nix { inherit nix_2_18; };

    inherit nix2container nixpkgs nixpkgs-regression;
  });
in
sc
