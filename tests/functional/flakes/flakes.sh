source ./common.sh

requireGit

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3
flake5Dir=$TEST_ROOT/flake5
flake7Dir=$TEST_ROOT/flake7
nonFlakeDir=$TEST_ROOT/nonFlake

for repo in $flake1Dir $flake2Dir $flake3Dir $flake7Dir $nonFlakeDir; do
    # Give one repo a non-main initial branch.
    extraArgs=
    if [[ $repo == $flake2Dir ]]; then
      extraArgs="--initial-branch=main"
    fi

    createGitRepo "$repo" "$extraArgs"
done

createSimpleGitFlake $flake1Dir

cat > $flake2Dir/flake.nix <<EOF
{
  description = "Fnord";

  outputs = { self, flake1 }: rec {
    packages.$system.bar = flake1.packages.$system.foo;
  };
}
EOF

git -C $flake2Dir add flake.nix
git -C $flake2Dir commit -m 'Initial'

cat > $flake3Dir/flake.nix <<EOF
{
  description = "Fnord";

  outputs = { self, flake2 }: rec {
    packages.$system.xyzzy = flake2.packages.$system.bar;

    checks = {
      xyzzy = packages.$system.xyzzy;
    };
  };
}
EOF

cat > $flake3Dir/default.nix <<EOF
{ x = 123; }
EOF

git -C $flake3Dir add flake.nix default.nix
git -C $flake3Dir commit -m 'Initial'

cat > $nonFlakeDir/README.md <<EOF
FNORD
EOF

git -C $nonFlakeDir add README.md
git -C $nonFlakeDir commit -m 'Initial'

# Construct a custom registry, additionally test the --registry flag
nix registry add --registry $registry flake1 git+file://$flake1Dir
nix registry add --registry $registry flake2 git+file://$flake2Dir
nix registry add --registry $registry flake3 git+file://$flake3Dir
nix registry add --registry $registry flake4 flake3
nix registry add --registry $registry nixpkgs flake1

json=$(nix flake metadata flake1 --json | jq .)
hash1=$(echo "$json" | jq -r .revision)

echo foo > $flake1Dir/foo
git -C $flake1Dir add $flake1Dir/foo
echo -n '# foo' >> $flake1Dir/flake.nix
git -C $flake1Dir commit -a -m 'Foo'
hash2=$(nix flake metadata flake1 --json --refresh | jq -r .revision)

# Check that store symlinks inside a flake are not interpreted as flakes.
nix build -o $flake1Dir/result git+file://$flake1Dir
nix path-info $flake1Dir/result

# set up lockfiles for later tests
nix build -o $TEST_ROOT/result $flake2Dir#bar --commit-lock-file
nix build -o $TEST_ROOT/result $flake3Dir#xyzzy
git -C $flake3Dir add flake.lock

# Add dependency to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  description = "Fnord";

  outputs = { self, flake1, flake2 }: rec {
    packages.$system.xyzzy = flake2.packages.$system.bar;
    packages.$system."sth sth" = flake1.packages.$system.foo;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Update flake.nix'

# Check whether `nix build` works with an incomplete lockfile
nix build -o $TEST_ROOT/result $flake3Dir#"sth sth"
nix build -o $TEST_ROOT/result $flake3Dir#"sth%20sth"

# Check whether it saved the lockfile
[[ -n $(git -C $flake3Dir diff master) ]]

git -C $flake3Dir add flake.lock

git -C $flake3Dir commit -m 'Add lockfile'

# Test whether flakes are registered as GC roots for offline use.
# FIXME: use tarballs rather than git.
rm -rf $TEST_HOME/.cache
nix store gc # get rid of copies in the store to ensure they get fetched to our git cache
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
mv $flake1Dir $flake1Dir.tmp
mv $flake2Dir $flake2Dir.tmp
nix store gc
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar
_NIX_FORCE_HTTP=1 nix build -o $TEST_ROOT/result git+file://$flake2Dir#bar --refresh
mv $flake1Dir.tmp $flake1Dir
mv $flake2Dir.tmp $flake2Dir

# Add nonFlakeInputs to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  inputs = {
    flake1 = {};
    flake2 = {};
    nonFlake = {
      url = "git+file://$nonFlakeDir";
      flake = false;
    };
    nonFlakeFile = {
      url = "path://$nonFlakeDir/README.md";
      flake = false;
    };
    nonFlakeFile2 = {
      url = "$nonFlakeDir/README.md";
      flake = false;
    };
  };

  description = "Fnord";

  outputs = inputs: rec {
    packages.$system.xyzzy = inputs.flake2.packages.$system.bar;
    packages.$system.sth = inputs.flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        dummy = builtins.readFile (builtins.path { name = "source"; path = ./.; filter = path: type: baseNameOf path == "config.nix"; } + "/config.nix");
        dummy2 = builtins.readFile (builtins.path { name = "source"; path = inputs.flake1; filter = path: type: baseNameOf path == "simple.nix"; } + "/simple.nix");
        buildCommand = ''
          cat \${inputs.nonFlake}/README.md > \$out
          [[ \$(cat \${inputs.nonFlake}/README.md) = \$(cat \${inputs.nonFlakeFile}) ]]
          [[ \${inputs.nonFlakeFile} = \${inputs.nonFlakeFile2} ]]
        '';
      };
  };
}
EOF

cp ../config.nix $flake3Dir

git -C $flake3Dir add flake.nix config.nix
git -C $flake3Dir commit -m 'Add nonFlakeInputs'

# Check whether `nix build` works with a lockfile which is missing a
# nonFlakeInputs.
nix build -o $TEST_ROOT/result $flake3Dir#sth --commit-lock-file
# check that the commit message is broadly correct. we can't check for
# exact contents of the message becase the build dirs change too much.
[[ "$(git -C $flake3Dir show -s --format=format:%B)" = \
"flake.lock: Update

Flake lock file updates:

• Added input 'nonFlake':
    'git+file://"*"/flakes/flakes/nonFlake?ref=refs/heads/master&rev="*"' "*"
• Added input 'nonFlakeFile':
    'path:"*"/flakes/flakes/nonFlake/README.md?lastModified="*"&narHash=sha256-cPh6hp48IOdRxVV3xGd0PDgSxgzj5N/2cK0rMPNaR4o%3D' "*"
• Added input 'nonFlakeFile2':
    'path:"*"/flakes/flakes/nonFlake/README.md?lastModified="*"&narHash=sha256-cPh6hp48IOdRxVV3xGd0PDgSxgzj5N/2cK0rMPNaR4o%3D' "* ]]

nix build -o $TEST_ROOT/result flake3#fnord
[[ $(cat $TEST_ROOT/result) = FNORD ]]

# Check whether flake input fetching is lazy: flake3#sth does not
# depend on flake2, so this shouldn't fail.
rm -rf $TEST_HOME/.cache
clearStore
mv $flake2Dir $flake2Dir.tmp
mv $nonFlakeDir $nonFlakeDir.tmp
nix build -o $TEST_ROOT/result flake3#sth
(! nix build -o $TEST_ROOT/result flake3#xyzzy)
(! nix build -o $TEST_ROOT/result flake3#fnord)
mv $flake2Dir.tmp $flake2Dir
mv $nonFlakeDir.tmp $nonFlakeDir
nix build -o $TEST_ROOT/result flake3#xyzzy flake3#fnord

# Test doing multiple `lookupFlake`s
nix build -o $TEST_ROOT/result flake4#xyzzy

# Test 'nix flake update' and --override-flake.
nix flake lock $flake3Dir
[[ -z $(git -C $flake3Dir diff master || echo failed) ]]

nix flake update --flake "$flake3Dir" --override-flake flake2 nixpkgs
[[ ! -z $(git -C "$flake3Dir" diff master || echo failed) ]]

# Make branch "removeXyzzy" where flake3 doesn't have xyzzy anymore
git -C $flake3Dir checkout -b removeXyzzy
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  inputs = {
    nonFlake = {
      url = "$nonFlakeDir";
      flake = false;
    };
  };

  description = "Fnord";

  outputs = { self, flake1, flake2, nonFlake }: rec {
    packages.$system.sth = flake1.packages.$system.foo;
    packages.$system.fnord =
      with import ./config.nix;
      mkDerivation {
        inherit system;
        name = "fnord";
        buildCommand = ''
          cat \${nonFlake}/README.md > \$out
        '';
      };
  };
}
EOF
nix flake lock $flake3Dir
git -C $flake3Dir add flake.nix flake.lock
git -C $flake3Dir commit -m 'Remove packages.xyzzy'
git -C $flake3Dir checkout master

# Test 'nix flake clone'.
rm -rf $TEST_ROOT/flake1-v2
nix flake clone flake1 --dest $TEST_ROOT/flake1-v2
[ -e $TEST_ROOT/flake1-v2/flake.nix ]

# Test 'follows' inputs.
cat > $flake3Dir/flake.nix <<EOF
{
  inputs.foo = {
    type = "indirect";
    id = "flake1";
  };
  inputs.bar.follows = "foo";

  outputs = { self, foo, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["foo"]' ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.bar.follows = "flake2/flake1";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["flake2","flake1"]' ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.bar.follows = "flake2";

  outputs = { self, flake2, bar }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq -c .nodes.root.inputs.bar $flake3Dir/flake.lock) = '["flake2"]' ]]

# Test overriding inputs of inputs.
writeTrivialFlake $flake7Dir
git -C $flake7Dir add flake.nix
git -C $flake7Dir commit -m 'Initial'

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.flake2.inputs.flake1 = {
    type = "git";
    url = "file://$flake7Dir";
  };

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake lock $flake3Dir
[[ $(jq .nodes.flake1.locked.url $flake3Dir/flake.lock) =~ flake7 ]]

cat > $flake3Dir/flake.nix <<EOF
{
  inputs.flake2.inputs.flake1.follows = "foo";
  inputs.foo.url = "git+file://$flake7Dir";

  outputs = { self, flake2 }: {
  };
}
EOF

nix flake update --flake "$flake3Dir"
[[ $(jq -c .nodes.flake2.inputs.flake1 "$flake3Dir/flake.lock") =~ '["foo"]' ]]
[[ $(jq .nodes.foo.locked.url "$flake3Dir/flake.lock") =~ flake7 ]]

# prepare path flakes.
mkdir -p $flake5Dir
writeDependentFlake $flake5Dir
nix flake lock path://$flake5Dir

# Test tarball flakes.
tar cfz $TEST_ROOT/flake.tar.gz -C $TEST_ROOT flake5

# Test --override-input.
git -C $flake3Dir reset --hard
nix flake lock $flake3Dir --override-input flake2/flake1 file://$TEST_ROOT/flake.tar.gz -vvvvv
[[ $(jq .nodes.flake1_2.locked.url $flake3Dir/flake.lock) =~ flake.tar.gz ]]

nix flake lock $flake3Dir --override-input flake2/flake1 flake1
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) =~ $hash2 ]]

nix flake lock $flake3Dir --override-input flake2/flake1 flake1/master/$hash1
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) =~ $hash1 ]]

nix flake lock $flake3Dir
[[ $(jq -r .nodes.flake1_2.locked.rev $flake3Dir/flake.lock) = $hash1 ]]

# Test updating an individual input of a flake lockfile.
nix flake update flake2/flake1 --flake "$flake3Dir"
[[ $(jq -r .nodes.flake1_2.locked.rev "$flake3Dir/flake.lock") =~ $hash2 ]]

# Test updating multiple inputs.
nix flake lock "$flake3Dir" --override-input flake1 flake1/master/$hash1
nix flake lock "$flake3Dir" --override-input flake2/flake1 flake1/master/$hash1
[[ $(jq -r .nodes.flake1.locked.rev "$flake3Dir/flake.lock") =~ $hash1 ]]
[[ $(jq -r .nodes.flake1_2.locked.rev "$flake3Dir/flake.lock") =~ $hash1 ]]

nix flake update flake1 flake2/flake1 --flake "$flake3Dir"
[[ $(jq -r .nodes.flake1.locked.rev "$flake3Dir/flake.lock") =~ $hash2 ]]
[[ $(jq -r .nodes.flake1_2.locked.rev "$flake3Dir/flake.lock") =~ $hash2 ]]
