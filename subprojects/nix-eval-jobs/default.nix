{
  stdenv,
  lib,
  nix,
  pkgs,
  srcDir ? ./.,
  callPackage,
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
    buildInputs = with pkgs; [
      nlohmann_json
      nix
      boost
    ];
    nativeBuildInputs =
      with pkgs;
      [
        meson
        pkg-config
        ninja
        # nlohmann_json can be only discovered via cmake files
        cmake
        # XXX: ew
        nix.passthru.capnproto-lix
      ]
      ++ (lib.optional stdenv.cc.isClang [ pkgs.clang-tools ]);

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
            pytest
          '';
          installPhase = "touch $out";
        })
      ) { };
    };

    meta = {
      description = "Hydra's builtin hydra-eval-jobs as a standalone";
      homepage = "https://github.com/nix-community/nix-eval-jobs";
      license = lib.licenses.gpl3;
      maintainers = with lib.maintainers; [
        adisbladis
        mic92
      ];
      platforms = lib.platforms.unix;
    };
  });
in
package
