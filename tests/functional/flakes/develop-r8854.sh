source ../common.sh

# regression test for #8854 (nix develop fails when lockfile is ignored)

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

# Create flake under test.
mkdir -p $TEST_HOME/t
cp ../shell-hello.nix ../config.nix $TEST_HOME/t
cat <<EOF >$TEST_HOME/t/flake.nix
{
    inputs.nixpkgs.url = "$TEST_HOME/nixpkgs";
    outputs = {self, nixpkgs}: {
      packages.$system.hello = (import ./config.nix).mkDerivation {
        name = "hello";
        outputs = [ "out" "dev" ];
        meta.outputsToInstall = [ "out" ];
        buildCommand = "";
      };
    };
}
EOF

# Create fake nixpkgs flake.
mkdir -p $TEST_HOME/nixpkgs
cp ../config.nix ../nix-shell/shell.nix $TEST_HOME/nixpkgs
cat <<EOF >$TEST_HOME/nixpkgs/flake.nix
{
    outputs = {self}: {
      legacyPackages.$system.bashInteractive = (import ./shell.nix {}).bashInteractive;
    };
}
EOF

cd $TEST_HOME/t

git init .
echo flake.lock > .gitignore
git add config.nix shell-hello.nix flake.nix .gitignore

# flake.lock is ignored, but nix develop should still not fail
nix develop .#hello <<<"true"

clearStore
