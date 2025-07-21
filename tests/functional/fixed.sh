source common.sh

clearStore

path=$(nix-store -q $(nix-instantiate fixed.nix -A good.0))

echo 'testing bad...'
nix-build fixed.nix -A bad --no-out-link && fail "should fail"

# Building with the bad hash should produce the "good" output path as
# a side-effect.
[[ -e $path ]]
nix path-info --json $path | grep fixed:md5:2qk15sxzzjlnpjk9brn7j8ppcd

echo 'testing good...'
nix-build fixed.nix -A good --no-out-link

echo 'testing --check...'
nix-build fixed.nix -A check --check && fail "should fail"

echo 'testing good2...'
nix-build fixed.nix -A good2 --no-out-link

echo 'testing reallyBad...'
nix-instantiate fixed.nix -A reallyBad && fail "should fail"

echo 'testing fixed with references...'
expectStderr 1 nix-build fixed.nix -A badReferences | grepQuiet "not allowed to refer to other store paths"

echo 'testing illegal references...'
# Fixed FOD hashes cannot be asserted because:
# - the store directory varies between the "Lix build sandbox environment" and a user test run
# - *-darwin has a different store location on the top of this in the sandbox (/private/tmp/...) causing further changes.
# Regex matching is the best we can afford.
expectStderr 102 nix-build fixed.nix -A illegalReferences | grep -z "the fixed-output derivation '$TEST_ROOT/store/[a-z0-9]*-illegal-reference.drv' must not reference store paths but 1 such references were found:.*$TEST_ROOT/store/[a-z0-9]*-fixed" > /dev/null

# While we're at it, check attribute selection a bit more.
echo 'testing attribute selection...'
test $(nix-instantiate fixed.nix -A good.1 | wc -l) = 1

# Test parallel builds of derivations that produce the same output.
# Only one should run at the same time.
echo 'testing parallelSame...'
clearStore
nix-build fixed.nix -A parallelSame --no-out-link -j2

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
