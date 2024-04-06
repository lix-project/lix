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

nar=0bylmx35yjy2b1b4k7gjsl7i4vc03cpmryb41grfb1mp40n3hifl.nar.xz

[ -e $cacheDir/nar/$nar ] || fail "long nar missing?"

xzcat $cacheDir/nar/$nar > $TEST_HOME/tmp
truncate -s $(( $(stat -c %s $TEST_HOME/tmp) - 10 )) $TEST_HOME/tmp
xz < $TEST_HOME/tmp > $cacheDir/nar/$nar

# Copying back '$path' from the binary cache. This should fail as it is truncated
if build --option substituters "$BINARY_CACHE" --option require-sigs false -j0; then
    fail "Importing a truncated nar should fail"
fi
