source common.sh

clearStore

rm -f $TEST_ROOT/result

export REMOTE_STORE=file:$TEST_ROOT/remote_store
echo 'require-sigs = false' >> $NIX_CONF_DIR/nix.conf

restartDaemon

pushToStore="$PWD/push-to-store.sh"

# Build the dependencies and push them to the remote store.
nix-build -o $TEST_ROOT/result dependencies.nix --post-build-hook "$pushToStore"
# See if all outputs are passed to the post-build hook by only specifying one
# We're not able to test CA tests this way
export BUILD_HOOK_ONLY_OUT_PATHS=$([ ! $NIX_TESTS_CA_BY_DEFAULT ])
nix-build -o $TEST_ROOT/result-mult multiple-outputs.nix -A a.first --post-build-hook "$pushToStore"

clearStore

# Ensure that the remote store contains both the runtime and build-time
# closure of what we've just built.
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix
nix copy --from "$REMOTE_STORE" --no-require-sigs -f dependencies.nix input1_drv
nix copy --from "$REMOTE_STORE" --no-require-sigs -f multiple-outputs.nix a^second

clearStore

# Should fail if the build hook fails
cat > "$TEST_ROOT/fail.sh" <<-EOF
#!${shell}
false
EOF
chmod +x "$TEST_ROOT/fail.sh"
expect 1 nix-build -o "$TEST_ROOT/result" dependencies.nix --post-build-hook "$TEST_ROOT/fail.sh"
clearStore

# Ensure that settings are passed into the post-build-hook, but only overridden
# ones.
rm -f "${TEST_ROOT}/nix-config"
cat > "${TEST_ROOT}/settings.sh" <<-EOF
#!${shell}
echo "\$NIX_CONFIG" > "${TEST_ROOT}/nix-config"
EOF
chmod +x "${TEST_ROOT}/settings.sh"
nix-build -o "${TEST_ROOT}/result" dependencies.nix --timeout 1337 --post-build-hook "${TEST_ROOT}/settings.sh"
# Ensure pure-eval cannot become not a setting with the test passing.
nix config show pure-eval
# Defaulted setting does not appear.
expect 1 grepQuiet pure-eval "${TEST_ROOT}/nix-config"
# Overridden setting appears.
grepQuiet "timeout = 1337" "${TEST_ROOT}/nix-config"
