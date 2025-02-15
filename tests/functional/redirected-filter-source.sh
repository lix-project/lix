# This exercises some of the behaviors we expect under redirected stores w.r.t. to `filterSource`.
# They serve as anti regression testing towards the `lib.fileset` breakage in nixpkgs.
# See: https://github.com/NixOS/nixpkgs/pull/369694.

source common.sh

TEST_STORE="$TEST_ROOT/teststore"
TEST_DIR="$TEST_ROOT/testsrc"

mkdir -p "$TEST_STORE"
mkdir -p "$TEST_DIR"
chmod -R u+w $TEST_STORE
rm -fr $TEST_STORE

cp redirected-filter-source.nix "$TEST_DIR/default.nix"

nix-instantiate --eval --store "$TEST_STORE" "$TEST_DIR" --arg path "\"$TEST_DIR\""

path_in_store="$(nix-store --add "$TEST_DIR")"
nix-store --store "$TEST_STORE" --add "$TEST_DIR" > /dev/null
nix-instantiate --eval --store "$TEST_STORE" "$TEST_DIR" --arg path "builtins.storePath \"$path_in_store\""
