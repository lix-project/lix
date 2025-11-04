source common.sh

BINARY_CACHE=file://$cacheDir?compression=none

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

nar=0513ia03lmqyq8bipmvv0awjji48li22rbmm9p5iwzm08y8m810z.nar

[ -e $cacheDir/nar/$nar ] || fail "long nar missing?"

truncate -s $(( $(stat -c %s $cacheDir/nar/$nar) - 10 )) $cacheDir/nar/$nar

# Copying back '$path' from the binary cache. This should fail as it is truncated
if build --option substituters "$BINARY_CACHE" --option require-sigs false -j0; then
    fail "Importing a truncated nar should fail"
fi
