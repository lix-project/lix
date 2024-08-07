{
  lib,
  stdenv,
  cmake,
  meson,
  ninja,
  llvmPackages,
}:
let
  inherit (lib) fileset;
in
stdenv.mkDerivation {
  pname = "lix-clang-tidy-checks";
  # Setting the version to the Lix version is just going to cause pointless
  # rebuilds due to versionSuffix and similar, and I cannot conceive of a usage
  # where we actually care about its version since this is internal-only.
  version = "0.1";

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      ./meson.options
      (fileset.fileFilter (
        { hasExt, ... }:
        builtins.any hasExt [
          "cc"
          "hh"
        ]
      ) ./.)
    ];
  };

  nativeBuildInputs = [
    meson
    cmake
    ninja
  ];

  buildInputs = [
    llvmPackages.llvm
    llvmPackages.clang-unwrapped.dev
  ];
}
