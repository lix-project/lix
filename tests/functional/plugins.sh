source common.sh

if [[ $BUILD_SHARED_LIBS != 1 ]]; then
    skipTest "Plugins are not supported"
fi

res=$(nix --option setting-set true --option plugin-files $PWD/plugins/libplugintest.* eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]

# Plugin load failing due to missing symbols
res=$(nix --option plugin-files $PWD/plugins/libplugintestfail.* eval --expr '1234 + 5' 2>&1)
# We expect this to succeed evaluating
echo "$res" | grep 1239 >/dev/null
# On Linux, we expect this to print some failure of dlopen.
# Only on Linux do we expect for sure that -z now is set on the .so file, so it
# will definitely fail to load instead of lazy loading (and thus not hitting
# the missing symbol).
# FIXME(jade): does there exist an equivalent of -z now on macOS that eluded us
# in search?
if [[ "$(uname -s)" == Linux ]]; then
    echo "$res" | grep "could not dynamically open plugin file" >/dev/null
fi
