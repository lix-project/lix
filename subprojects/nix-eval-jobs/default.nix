{
  stdenv,
  lib,
  nix,
  srcDir ? ./.,
  callPackage,
  meson,
  pkg-config,
  ninja,
  cmake,
  capnproto,
  clang-tools,
  nlohmann_json,
  boost,
}:

let
  sourceBase = if srcDir == null then ./. else srcDir;
  package = stdenv.mkDerivation (finalAttrs: {
    pname = "nix-eval-jobs";
    version = "2.93.0-dev";
    src = lib.fileset.toSource {
      root = sourceBase;
      fileset = lib.fileset.unions [
        ./meson.build
        ./src
      ];
    };
    buildInputs = [
      nlohmann_json
      nix
      boost
      capnproto
    ];
    nativeBuildInputs = [
      meson
      pkg-config
      ninja
      # nlohmann_json can be only discovered via cmake files
      cmake
      capnproto
    ]
    ++ (lib.optional stdenv.cc.isClang [ clang-tools ]);

    passthru = {
      inherit nix;
      tests.nix-eval-jobs = callPackage (
        {
          stdenv,
          python3Packages,
          path,
        }:
        let
          nejAttrs = finalAttrs;
        in
        stdenv.mkDerivation (finalAttrs: {
          pname = "nix-eval-jobs-tests";
          inherit (nejAttrs) version;
          src = lib.fileset.toSource {
            root = sourceBase;
            fileset = lib.fileset.unions [
              ./pyproject.toml
              ./tests
            ];
          };

          nativeBuildInputs = [
            python3Packages.pytest
            package
          ];

          env.NEJ_NIXPKGS_PATH = "${path}";

          dontConfigure = true;
          buildPhase = ''
            export NIX_REMOTE="$NIX_BUILD_TOP"
            export HOME="$(mktemp -d)"
            pytest -v
          '';
          installPhase = "touch $out";
        })
      ) { };
    };

    meta = {
      description = "Hydra's builtin hydra-eval-jobs as a standalone";
      homepage = "https://git.lix.systems/lix-project/lix/src/branch/main/subprojects/nix-eval-jobs";
      license = lib.licenses.gpl3;
      platforms = lib.platforms.unix;
    };
  });
in
package
