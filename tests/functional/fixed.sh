source common.sh

clearStore

# Fixed-output derivations with a recursive SHA-256 hash should
# produce the same path as "nix-store --add".
echo 'testing sameAsAdd...'
out=$(nix-build fixed.nix -A sameAsAdd --no-out-link)

# This is what fixed.builder2 produces...
rm -rf $TEST_ROOT/fixed
mkdir $TEST_ROOT/fixed
mkdir $TEST_ROOT/fixed/bla
echo "Hello World!" > $TEST_ROOT/fixed/foo
ln -s foo $TEST_ROOT/fixed/bar

out2=$(nix-store --add $TEST_ROOT/fixed)
[ "$out" = "$out2" ]

out3=$(nix-store --add-fixed --recursive sha256 $TEST_ROOT/fixed)
[ "$out" = "$out3" ]

out4=$(nix-store --print-fixed-path --recursive sha256 "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik" fixed)
[ "$out" = "$out4" ]
