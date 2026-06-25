source common.sh

requireGit

clearStore

# Intentionally not in a canonical form
# See https://github.com/NixOS/nix/issues/6195
repo=$TEST_ROOT/./git

export _NIX_FORCE_HTTP=1

rm -rf $repo ${repo}-tmp $TEST_HOME/.cache/nix $TEST_ROOT/worktree $TEST_ROOT/shallow $TEST_ROOT/minimal

git init $repo
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

echo utrecht > $repo/hello
touch $repo/.gitignore
git -C $repo add hello .gitignore
git -C $repo commit -m 'Bla1'
rev1=$(git -C $repo rev-parse HEAD)
git -C $repo tag -a tag1 -m tag1

echo world > $repo/hello
git -C $repo commit -m 'Bla2' -a
git -C $repo worktree add $TEST_ROOT/worktree
echo hello >> $TEST_ROOT/worktree/hello
rev2=$(git -C $repo rev-parse HEAD)
git -C $repo tag -a tag2 -m tag2

# Fetch a worktree
unset _NIX_FORCE_HTTP
path0=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$TEST_ROOT/worktree\").outPath")
export _NIX_FORCE_HTTP=1
path=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# Fetch a rev from another branch
git -C $repo checkout -b devtest
echo "different file" >> $TEST_ROOT/git/differentbranch
git -C $repo add differentbranch
git -C $repo commit -m 'Test2'
git -C $repo checkout master
devrev=$(git -C $repo rev-parse devtest)

# In pure eval mode, fetchGit with a revision should succeed.
[[ $(nix eval --raw --expr "builtins.readFile (fetchGit { url = \"file://$repo\"; rev = \"$rev2\"; } + \"/hello\")") = world ]]

# Using an unclean tree should yield the tracked but uncommitted changes.
mkdir $repo/dir1 $repo/dir2
echo foo > $repo/dir1/foo
echo bar > $repo/bar
echo bar > $repo/dir2/bar
git -C $repo add dir1/foo
git -C $repo rm hello

unset _NIX_FORCE_HTTP
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")

# Committing should not affect the store path.
git -C $repo commit -m 'Bla3' -a

# ---------------------------------------------------------
#
# tarball-ttl should be ignored if we specify a rev
echo delft > $repo/hello
git -C $repo add hello
git -C $repo commit -m 'Bla4'
rev3=$(git -C $repo rev-parse HEAD)
nix eval --tarball-ttl 3600 --expr "builtins.fetchGit { url = $repo; rev = \"$rev3\"; }" >/dev/null

# Update 'path' to reflect latest master
path=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# Check behavior when non-master branch is used
git -C $repo checkout $rev2 -b dev
echo dev > $repo/hello

# File URI uses dirty tree unless specified otherwise
path2=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")
[ $(cat $path2/hello) = dev ]

# Using local path with branch other than 'master' should work when clean or dirty
path3=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
# (check dirty-tree handling was used)
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = 0000000000000000000000000000000000000000 ]]
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).shortRev") = 0000000 ]]
# Making a dirty tree clean again and fetching it should
# record correct revision information. See: #4140
echo world > $repo/hello
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit $repo).rev") = $rev2 ]]

# Committing shouldn't change store path, or switch to using 'master'
echo dev > $repo/hello
git -C $repo commit -m 'Bla5' -a
path4=$(nix eval --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $(cat $path4/hello) = dev ]]
[[ $path3 = $path4 ]]

# Using remote path with branch other than 'master' should fetch the HEAD revision.
# (--tarball-ttl 0 to prevent using the cached repo above)
export _NIX_FORCE_HTTP=1
path4=$(nix eval --tarball-ttl 0 --impure --raw --expr "(builtins.fetchGit $repo).outPath")
[[ $(cat $path4/hello) = dev ]]
[[ $path3 = $path4 ]]
unset _NIX_FORCE_HTTP

# Confirm same as 'dev' branch
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]


# Nuke the cache
rm -rf $TEST_HOME/.cache/nix

# Try again, but without 'git' on PATH. This should fail.
NIX=$(command -v nix)
(! PATH= $NIX eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath" )

# Try again, with 'git' available.  This should work.
path5=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = $repo; ref = \"dev\"; }).outPath")
[[ $path3 = $path5 ]]

# Fetching from a repo with only a specific revision and no branches should
# not fall back to copying files and record correct revision information. See: #5302
mkdir $TEST_ROOT/minimal
git -C $TEST_ROOT/minimal init
git -C $TEST_ROOT/minimal fetch $repo $rev2
git -C $TEST_ROOT/minimal checkout $rev2
[[ $(nix eval --impure --raw --expr "(builtins.fetchGit { url = $TEST_ROOT/minimal; }).rev") = $rev2 ]]

# Fetching a shallow repo shouldn't work by default, because we can't
# return a revCount.
git clone --depth 1 file://$repo $TEST_ROOT/shallow
(! nix eval --impure --raw --expr "(builtins.fetchGit { url = $TEST_ROOT/shallow; ref = \"dev\"; }).outPath")

# But you can request a shallow clone, which won't return a revCount.
path6=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow\"; ref = \"dev\"; shallow = true; }).outPath")
[[ $path3 = $path6 ]]
[[ $(nix eval --impure --expr "(builtins.fetchTree { type = \"git\"; url = \"file://$TEST_ROOT/shallow\"; ref = \"dev\"; shallow = true; }).revCount or 123") == 123 ]]

# Explicit ref = "HEAD" should work, and produce the same outPath as without ref
path7=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).outPath")
path8=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; }).outPath")
[[ $path7 = $path8 ]]

# ref = "HEAD" should fetch the HEAD revision
rev4=$(git -C $repo rev-parse HEAD)
rev4_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; }).rev")
[[ $rev4 = $rev4_nix ]]

# The name argument should be handled
path9=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"HEAD\"; name = \"foo\"; }).outPath")
[[ $path9 =~ -foo$ ]]

# Specifying a ref without a rev shouldn't pick a cached rev for a different ref
export _NIX_FORCE_HTTP=1
rev_tag1_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/tags/tag1\"; }).rev")
rev_tag1=$(git -C $repo rev-list --max-count=1 refs/tags/tag1)
[[ $rev_tag1_nix = $rev_tag1 ]]

# Allow fetching tags w/o specifying refs/tags
rm -rf "$TEST_ROOT/test-home"
rev_tag1_nix_alt=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"tag1\"; }).rev")
[[ $rev_tag1_nix_alt = $rev_tag1 ]]

rev_tag2_nix=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/tags/tag2\"; }).rev")
rev_tag2=$(git -C $repo rev-list --max-count=1 refs/tags/tag2)
[[ $rev_tag2_nix = $rev_tag2 ]]
unset _NIX_FORCE_HTTP

# should fail if there is no repo
rm -rf $repo/.git
(! nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# should succeed for a repo without commits
git init $repo
path10=$(nix eval --impure --raw --expr "(builtins.fetchGit \"file://$repo\").outPath")

# should succeed for a path with a space
# regression test for #7707
repo="$TEST_ROOT/a b"
git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

echo utrecht > "$repo/hello"
touch "$repo/.gitignore"
git -C "$repo" add hello .gitignore
git -C "$repo" commit -m 'Bla1'
cd "$repo"
path11=$(nix eval --impure --raw --expr "(builtins.fetchGit ./.).outPath")

# test behavior if both branch and tag with same name exist
repo="$TEST_ROOT/git"
rm -rf "$repo"/.git
git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"

touch "$repo"/test
echo "hello world" > "$repo"/test
git -C "$repo" checkout -b branch
git -C "$repo" add test

git -C "$repo" commit -m "Init"

git -C "$repo" tag branch

echo "goodbye world" > "$repo"/test
git -C "$repo" add test
git -C "$repo" commit -m "Update test"

path12=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"branch\"; }).outPath")
[[ "$(cat "$path12"/test)" =~ 'hello world' ]]
[[ "$(cat "$repo"/test)" =~ 'goodbye world' ]]

path13=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/heads/branch\"; }).outPath")
[[ "$(cat "$path13"/test)" =~ 'goodbye world' ]]

path14=$(nix eval --impure --raw --expr "(builtins.fetchGit { url = \"file://$repo\"; ref = \"refs/tags/branch\"; }).outPath")
[[ "$path14" = "$path12" ]]
