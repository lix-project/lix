source common.sh

requireGit

clearStore

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1

rm -rf "$repo" "$TEST_HOME/.cache/nix"

mkdir "$repo" && pushd "$repo"

git init --initial-branch=main
git config user.email "foobar@example.com"
git config user.name "Foobar"

echo utrecht >hello
touch .gitignore
git add hello .gitignore
git commit -m 'Bla1'
rev1=$(git rev-parse HEAD)
git tag -a tag1 -m tag1

# Compute the hash of the output
path=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")
hash=$(nix-hash --type sha256 --base32 "$path")
narHash=$(nix-hash --to-sri --type sha256 "$hash")

# Remove the repo, and the local cache
popd && rm -rf "$repo" "$TEST_HOME/.cache/nix"

# The path is locked and can be fetched
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"main\"; rev=\"$rev1\"; narHash = \"$narHash\"; })")
[[ "$path" = "$path2" ]]

# When no narHash is present the fetching fails
! nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"main\"; rev=\"$rev1\"; })"
