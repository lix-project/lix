source common.sh

BINARY_CACHE=file://$cacheDir

build() {
    nix-build --no-out-link "$@" --expr 'derivation {
        name = "text";
        system = builtins.currentSystem;
        builder = "/bin/sh";
        args = [ "-c" "echo some text to make the nar less empty > $out" ];
    }'
}

path=$(build)
nix copy --to "$BINARY_CACHE" "$path"
nix-collect-garbage >/dev/null 2>&1

nar=0c3y7p42issm0ydjilwvk0drv958p4p4d2d6c7y5ksmzmbf7rfhg.nar.zst

[ -e $cacheDir/nar/$nar ] || fail "long nar missing?"

zstdcat $cacheDir/nar/$nar > $TEST_HOME/tmp
truncate -s $(( $(stat -c %s $TEST_HOME/tmp) - 10 )) $TEST_HOME/tmp
zstd - --stdout < $TEST_HOME/tmp > $cacheDir/nar/$nar

# Copying back '$path' from the binary cache. This should fail as it is truncated
if build --option substituters "$BINARY_CACHE" --option require-sigs false -j0; then
    fail "Importing a truncated nar should fail"
fi
