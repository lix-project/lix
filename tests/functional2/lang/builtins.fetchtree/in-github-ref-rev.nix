# tests that this produces a proper error, as it didn't before
# https://git.lix.systems/lix-project/lix/issues/1133
builtins.fetchTree {
  type = "github";
  owner = "nixos";
  repo = "nixpkgs";
  ref = "nixpkgs-unstable";
  rev = "e4bae1bd10c9c57b2cf517953ab70060a828ee6f";
}
