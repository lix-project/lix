{ lib, fileset
, stdenv
, perl, perlPackages
, autoconf-archive, autoreconfHook, pkg-config
, nix, curl, bzip2, xz, boost, libsodium, darwin
, meson
, ninja
, buildWithMeson ? false,
}:

perl.pkgs.toPerlModule (stdenv.mkDerivation {
  name = "nix-perl-${nix.version}";

  src = fileset.toSource {
    root = ../.;
    fileset = fileset.unions ([
      ../.version
      ./lib
    ] ++ lib.optionals (!buildWithMeson) [
      # FIXME(Qyriad): What the hell is this?
      # What is it used for and do we still need it?
      ./MANIFEST
      ../m4
      ../mk
      ./Makefile
      ./Makefile.config.in
      ./configure.ac
      ./local.mk
    ] ++ lib.optionals buildWithMeson [
      ./meson.build
    ]);
  };

  nativeBuildInputs = [
    pkg-config
  ] ++ lib.optionals (!buildWithMeson) [
    autoconf-archive
    autoreconfHook
  ] ++ lib.optionals buildWithMeson [
    meson
    ninja
  ];

  buildInputs =
    [ nix
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
})
