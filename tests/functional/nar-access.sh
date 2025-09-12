source common.sh

echo "building test path"
TEST_FILES_ROOT="$PWD"

storePath="$(nix-build nar-access.nix -A a --no-out-link)"

cd "$TEST_ROOT"

# Dump path to nar.
narFile="$TEST_ROOT/path.nar"
nix-store --dump $storePath > $narFile

# Check that find and nar ls match.
( cd $storePath; find . | sort ) > files.find
nix nar ls -R -d $narFile "" | sort > files.ls-nar
diff -u files.find files.ls-nar

# Check that file contents of data match.
nix nar cat $narFile /foo/data > data.cat-nar
diff -u data.cat-nar $storePath/foo/data

# Check that file contents of baz match.
nix nar cat $narFile /foo/baz > baz.cat-nar
diff -u baz.cat-nar $storePath/foo/baz

nix store cat $storePath/foo/baz > baz.cat-nar
diff -u baz.cat-nar $storePath/foo/baz

# Check that 'nix store cat' fails on invalid store paths.
invalidPath="$(dirname $storePath)/99999999999999999999999999999999-foo"
cp -r $storePath $invalidPath
expect 1 nix store cat $invalidPath/foo/baz

# Test --json.
diff -u \
    <(nix nar ls --json $narFile / | jq -S) \
    <(echo '{"type":"directory","entries":{"foo":{},"foo-x":{},"qux":{},"zyx":{}}}' | jq -S)
diff -u \
    <(nix nar ls --json -R $narFile /foo | jq -S) \
    <(echo '{"type":"directory","entries":{"bar":{"type":"regular","size":0,"narOffset":368},"baz":{"type":"regular","size":0,"narOffset":552},"data":{"type":"regular","size":58,"narOffset":736}}}' | jq -S)
diff -u \
    <(nix nar ls --json -R $narFile /foo/bar | jq -S) \
    <(echo '{"type":"regular","size":0,"narOffset":368}' | jq -S)
diff -u \
    <(nix store ls --json $storePath | jq -S) \
    <(echo '{"type":"directory","entries":{"foo":{},"foo-x":{},"qux":{},"zyx":{}}}' | jq -S)
diff -u \
    <(nix store ls --json -R $storePath/foo | jq -S) \
    <(echo '{"type":"directory","entries":{"bar":{"type":"regular","size":0},"baz":{"type":"regular","size":0},"data":{"type":"regular","size":58}}}' | jq -S)
diff -u \
    <(nix store ls --json -R $storePath/foo/bar| jq -S) \
    <(echo '{"type":"regular","size":0}' | jq -S)

# Test missing files.
expect 1 nix store ls --json -R $storePath/xyzzy 2>&1 | grep 'does not exist in NAR'
expect 1 nix store ls $storePath/xyzzy 2>&1 | grep 'does not exist'

# Test failure to dump.
if nix-store --dump $storePath >/dev/full ; then
    echo "dumping to /dev/full should fail"
    exit -1
fi

# compressed nars should cause a useful error message. we use ascii
# text input because the salient point is the initial length field.
echo 'not a real nar' > bad.nar
expect 1 nix nar ls bad.nar / 2>&1 | fgrep "doesn't look like a Nix archive (found malformed string tag"


# Test reading from remote nar listings if available
nix copy --to "file://$cacheDir?write-nar-listing=true" $storePath

if canWriteNonUtf8Inodes; then
    strangerStorePath="$(nix-build "$TEST_FILES_ROOT/nar-access.nix" -A a --arg nonUtf8Inodes true --no-out-link)"
    expect 0 nix copy --to "file://$cacheDir?write-nar-listing=true" $strangerStorePath 2>&1 | grep "warning: Skipping NAR listing for path"
    # Confirm that NARs with non-UTF8 inodes can still be listed
    expect 0 nix store ls $strangerStorePath/ --store "file://$cacheDir"
fi

export _NIX_FORCE_HTTP=1

diff -u \
    <(nix store ls --json $storePath --store "file://$cacheDir" | jq -S) \
    <(echo '{"type":"directory","entries":{"foo":{},"foo-x":{},"qux":{},"zyx":{}}}' | jq -S)
diff -u \
    <(nix store ls --json -R $storePath/foo/bar --store "file://$cacheDir" | jq -S) \
    <(echo '{"narOffset": 368,"type":"regular","size":0}' | jq -S)


# Confirm that we are reading from ".ls" file by moving the nar
mv $cacheDir/nar $cacheDir/nar.gone
diff -u \
    <(nix store ls --json -R $storePath/foo/bar --store "file://$cacheDir" | jq -S) \
    <(echo '{"narOffset": 368,"type":"regular","size":0}' | jq -S)
mv $cacheDir/nar.gone $cacheDir/nar

# confirm that we read the nar if the listing is missing offsets
narls=$(echo "$cacheDir/"*.ls)
cp "$narls" "$narls.old"
jq 'walk(if type == "object" then del(.narOffset) else . end)' < "$narls.old" >"$narls"
diff -u \
    <(nix store ls --json $storePath/foo/bar --store "file://$cacheDir" | jq -S) \
    <(echo '{"narOffset": 368,"type":"regular","size":0}' | jq -S)
mv "$narls.old" "$narls"

if canWriteNonUtf8Inodes; then
    # Confirm that there's no more than one `.ls` in the `$cacheDir` because non-UTF8 inodes cannot have `.ls` generated for them.
    [[ $(find $cacheDir -type f -name '*.ls' | wc -l) -eq 1 ]] || (echo "Expected at most one listing file in $cacheDir, found more"; exit -1)
fi
