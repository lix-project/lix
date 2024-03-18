{
  rustPlatform,
  lib
}:

rustPlatform.buildRustPackage {
  name = "nix-doc";

  cargoHash = "sha256-HXL235loBJnRje7KaMCCCTighv6WNYRrZ/jgkAQbEY0=";
  src = lib.cleanSource ./.;
}
