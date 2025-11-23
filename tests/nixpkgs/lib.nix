{
  lib,
  stdenv,
  symlinkJoin,

  pkgs,

  nix,
  nixpkgs,
  system,
}:

# NOTE: Every endpoint used here is very fragile as nixpkgs is not built
#       to test itself with anything other than CppNix. It will probably
#       break during some nixpkgs bump.

let
  inherit (lib) concatStringsSep optionals;

  # Deprecated features to enable while running the nixpkgs test suite
  # FIXME: All of these are fixed in Nixpkgs already, so clear the list on the next `nixpkgs` bump
  deprecatedFeatures = [
    "broken-string-indentation"
    "broken-string-escape"
    "rec-set-merges"
  ];

  env.NIX_CONFIG = "extra-deprecated-features = ${concatStringsSep " " deprecatedFeatures}";
in

symlinkJoin {
  name = "nixpkgs-lib-tests";
  paths =
    map
      (
        drv:
        drv.overrideAttrs (old: {
          env = (old.env or { }) // env;
        })
      )
      (
        [
          (
            (import (nixpkgs + "/lib/tests/test-with-nix.nix") {
              inherit pkgs lib nix;
            }).overrideAttrs
            (_: {
              buildInputs = [
                (import (nixpkgs + "/lib/path/tests") {
                  inherit pkgs;
                  # NOTE: Override the nix version used here, we cannot rely on CppNix actually building
                  #       c.f. https://github.com/NixOS/nixpkgs/blob/master/lib/path/tests/default.nix#L18
                  nixVersions.stable = nix;
                })
              ];
            })
          )
        ]
        ++ optionals stdenv.isLinux [
          ((pkgs.callPackage (nixpkgs + "/ci/eval") { inherit nix; } { }).attrpathsSuperset {
            evalSystem = system;
          })
        ]
      );
}
