{
  lib,
  fileset,
  stdenv,
  perl,
  perlPackages,
  pkg-config,
  nix,
  curl,
  bzip2,
  xz,
  boost,
  libsodium,
  darwin,
  meson,
  ninja,
}:

perl.pkgs.toPerlModule (
  stdenv.mkDerivation {
    name = "nix-perl-${nix.version}";

    src = fileset.toSource {
      root = ../.;
      fileset = fileset.unions ([
        ../.version
        ./lib
        ./meson.build
      ]);
    };

    nativeBuildInputs = [
      pkg-config
      meson
      ninja
    ];

    buildInputs =
      [
        nix
        curl
        bzip2
        xz
        perl
        boost
        perlPackages.DBI
        perlPackages.DBDSQLite
      ]
      ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
      ++ lib.optional stdenv.isDarwin darwin.apple_sdk.frameworks.Security;

    # Nixpkgs' Meson hook likes to set this to "plain".
    mesonBuildType = "debugoptimized";

    enableParallelBuilding = true;

    postUnpack = "sourceRoot=$sourceRoot/perl";
  }
)
