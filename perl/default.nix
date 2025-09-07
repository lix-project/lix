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
  darwin,
  meson,
  ninja,
  capnproto,
}:

perl.pkgs.toPerlModule (
  stdenv.mkDerivation {
    name = "nix-perl-${nix.version}";

    src = fileset.toSource {
      root = ../.;
      fileset = fileset.unions ([
        ../version.json
        ./lib
        ./meson.build
      ]);
    };

    nativeBuildInputs = [
      pkg-config
      meson
      ninja
    ];

    buildInputs = [
      nix
      curl
      bzip2
      xz
      perl
      boost
      perlPackages.DBI
      perlPackages.DBDSQLite
      # for kj-async
      capnproto
    ]
    ++ lib.optional stdenv.isDarwin darwin.apple_sdk.frameworks.Security;

    # Nixpkgs' Meson hook likes to set this to "plain".
    mesonBuildType = "debugoptimized";

    enableParallelBuilding = true;

    postUnpack = "sourceRoot=$sourceRoot/perl";

    passthru = { inherit perl; };
  }
)
