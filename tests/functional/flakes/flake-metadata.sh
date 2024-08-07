source ./common.sh

flakeDir=$TEST_ROOT/flake
mkdir -p "$flakeDir"

cat > "$flakeDir/flake.nix" <<-'EOF'
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable-small";
    flake-utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
    lanzaboote = {
      url = "github:nix-community/lanzaboote";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-utils.follows = "flake-utils";
      inputs.flake-compat.follows = "flake-compat";
    };
  };

  outputs = { ... }: {};
}
EOF

cp flake-metadata/flake.lock "$flakeDir"
touch -d @1000 "$flakeDir/flake.nix" "$flakeDir/flake.lock" "$flakeDir"

# For some reason we use NIX_STORE_DIR which causes unstable paths. This is
# goofy. We can just use --store, which sets rootDir and does not have this
# problem.
actualStore=$NIX_STORE_DIR
unset NIX_STORE_DIR
NOCOLOR=1 TZ=UTC LC_ALL=C.UTF-8 nix flake metadata --store "$actualStore" "$flakeDir" | grep -v -e 'Locked URL:' -e 'Resolved URL:' > "$TEST_ROOT/metadata.out.actual"
diff -u flake-metadata/metadata.out.expected "$TEST_ROOT/metadata.out.actual"
