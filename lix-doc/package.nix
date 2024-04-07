{
  rustPlatform,
  lib
}:

rustPlatform.buildRustPackage {
  name = "lix-doc";

  cargoLock.lockFile = ./Cargo.lock;
  src = lib.cleanSource ./.;
}
