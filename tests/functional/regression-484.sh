source common.sh

# Ensures that nix build will deliver a usable error message when it encounters
# a build failure potentially caused by allowSubstitutes.

clearStore

cd $TEST_HOME

putDrv() {
    cat > "$1" <<EOF
builtins.derivation {
  name = "meow";
  builder = "/bin/sh";
  args = [];
  system = "unicornsandrainbows-linux";
  allowSubstitutes = ${2};
  preferLocalBuild = true;
}
EOF
}

putDrv drv-disallow-substitutes.nix false
putDrv drv-allow-substitutes.nix true

expect 1 nix build --substitute --substituters '' --offline -f drv-disallow-substitutes.nix 2> output.txt

# error: a 'unicornsandrainbows-linux' with features {} is required to build '$TMPDIR/regression-484/store/...-meow.drv', but I am a 'x86_64-linux' with features {benchmark, big-parallel, kvm, nixos-test, uid-range}
#
#        Hint: the failing derivation has allowSubstitutes set to false, forcing it to be built rather than substituted.
#        Passing --always-allow-substitutes to force substitution may resolve this failure if the path is available in a substituter.
cat output.txt
grepQuiet unicornsandrainbows-linux output.txt
grepQuiet always-allow-substitutes output.txt
grepQuiet allowSubstitutes output.txt

expect 1 nix build --substitute --substituters '' --offline -f drv-allow-substitutes.nix 2> output.txt

cat output.txt
grepQuiet unicornsandrainbows-linux output.txt
grepQuiet -v allowSubstitutes output.txt
