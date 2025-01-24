source common.sh
# Regression test for https://git.lix.systems/lix-project/lix/issues/647

# Create the binary cache.
clearStore
clearCache
outPath=$(nix-build dependencies.nix --no-out-link)

nix key generate-secret --key-name test > "$TEST_ROOT/priv.key"
pubkey="$(nix key convert-secret-to-public < "$TEST_ROOT/priv.key")"
_NIX_FORCE_HTTP= nix copy --to file://$cacheDir?secret-key="$TEST_ROOT/priv.key" "$outPath"

clearStore
startDaemon

expect 1 nix-store -r --substituters file://$cacheDir "$outPath"
nix-store -r --trusted-public-keys "$pubkey" --substituters file://$cacheDir "$outPath"
