source common.sh

clearStore

## Test `nix-collect-garbage --dry-run`


testCollectGarbageDryRun () {
    clearProfiles
    # Install then uninstall a package
    # This should leave packages ready to be garbage collected.
    nix-env -f ./user-envs.nix -i foo-1.0
    nix-env -f ./user-envs.nix -e foo-1.0


    nix-env --delete-generations old
    [[ $(nix-store --gc --print-dead | wc -l) -eq 7 ]]

    nix-collect-garbage --dry-run
    [[ $(nix-store --gc --print-dead | wc -l) -eq 7 ]]

}

testCollectGarbageDryRun

# Run the same test, but forcing the profiles an arbitrary location.
rm ~/.nix-profile
ln -s $TEST_ROOT/blah ~/.nix-profile
testCollectGarbageDryRun

# Run the same test, but forcing the profiles at their legacy location under
# /nix/var/nix.
#
# Note that we *don't* use the default profile; `nix-collect-garbage` will
# need to check the legacy conditional unconditionally not just follow
# `~/.nix-profile` to pass this test.
#
# Regression test for #8294
rm ~/.nix-profile
testCollectGarbageDryRun --profile "$NIX_STATE_DIR/profiles/per-user/me"
