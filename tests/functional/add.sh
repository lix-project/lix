source common.sh

expectStderr 1 nix-store --add-fixed sha256 . | grep "cannot import directory using flat ingestion"
expectStderr 1 nix-store --add-fixed sha256 ./symlink | grep "cannot import symlink using flat ingestion"

expectStderr 1 nix-store --add-fixed sha1 . | grep "cannot import directory using flat ingestion"
expectStderr 1 nix-store --add-fixed sha1 ./symlink | grep "cannot import symlink using flat ingestion"

path1=$(nix-store --add ./dummy)
echo $path1

path2=$(nix-store --add-fixed sha256 --recursive ./dummy)
echo $path2

if test "$path1" != "$path2"; then
    echo "nix-store --add and --add-fixed mismatch"
    exit 1
fi

path3=$(nix-store --add-fixed sha256 ./dummy)
echo $path3
test "$path1" != "$path3" || exit 1

path4=$(nix-store --add-fixed sha1 --recursive ./dummy)
echo $path4
test "$path1" != "$path4" || exit 1

hash1=$(nix-store -q --hash $path1)
echo $hash1

hash2=$(nix-hash --type sha256 --base32 ./dummy)
echo $hash2

test "$hash1" = "sha256:$hash2"
