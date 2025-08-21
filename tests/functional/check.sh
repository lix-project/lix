source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
buggyNeedLocalStore "see #4813"

checkBuildTempDirRemoved ()
{
    buildDir=$(sed -n 's/CHECK_TMPDIR=//p' $1 | head -1)
    checkBuildIdFile=${buildDir}/checkBuildId
    [[ ! -f $checkBuildIdFile ]] || ! grep $checkBuildId $checkBuildIdFile
}

# written to build temp directories to verify created by this instance
checkBuildId=$(date +%s%N)

clearStore

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

# Build failure exit codes (100, 104, etc.) are from
# doc/manual/src/command-ref/status-build-failure.md

# check for dangling temporary build directories
# only retain if build fails and --keep-failed is specified, or...
# ...build is non-deterministic and --check and --keep-failed are both specified
nix-build check.nix -A failed --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log || status=$?
[ "$status" = "100" ]
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A failed --argstr checkBuildId $checkBuildId \
    --no-out-link --keep-failed 2> $TEST_ROOT/log || status=$?
[ "$status" = "100" ]
if checkBuildTempDirRemoved $TEST_ROOT/log; then false; fi

test_custom_build_dir() {
  local customBuildDir="$TEST_ROOT/custom-build-dir"

  # Nix does not create the parent directories, and perhaps it shouldn't try to
  # decide the permissions of build-dir.
  mkdir "$customBuildDir"
  nix-build check.nix -A failed --argstr checkBuildId $checkBuildId \
      --no-out-link --keep-failed --option build-dir "$TEST_ROOT/custom-build-dir" 2> $TEST_ROOT/log || status=$?
  [ "$status" = "100" ]
  [[ 1 == "$(count "$customBuildDir/nix-build-"*)" ]]
  local buildDir="$customBuildDir/nix-build-"*
  grep $checkBuildId $buildDir/b/checkBuildId
}
test_custom_build_dir

test_shell_preserves_tmpdir() {
  # ensure commands that spawn interactive shells don't overwrite TMPDIR with temp-dir
  local envTempDir=$TEST_ROOT/shell-temp-dir-env
  mkdir $envTempDir
  local settingTempDir=$TEST_ROOT/shell-temp-dir-setting
  mkdir $settingTempDir

  # FIXME: switch to check.nix's deterministic once `nix develop` doesn't need `outputs`
  # https://git.lix.systems/lix-project/lix/issues/556
  local expr='with import ./config.nix; mkDerivation { name = "foo"; buildCommand = "echo foo > $out"; outputs = [ "out" ]; }'

  local output
  output=$(TMPDIR=$envTempDir NIX_BUILD_SHELL=$SHELL nix-shell -E "$expr" --option temp-dir "$settingTempDir" --command 'echo $TMPDIR' 2> $TEST_ROOT/log)
  [[ $output = "$envTempDir" ]]

  output=$(TMPDIR=$envTempDir nix develop --impure -E "$expr" --option temp-dir "$settingTempDir" --command bash -c 'echo $TMPDIR' 2> $TEST_ROOT/log)
  [[ $output = "$envTempDir"/nix-shell.* ]]

  output=$(TMPDIR=$envTempDir nix shell --impure -E "$expr" --option temp-dir "$settingTempDir" --command bash -c 'echo $TMPDIR' 2> $TEST_ROOT/log)
  [[ $output = "$envTempDir" ]]
}
test_shell_preserves_tmpdir

nix-build check.nix -A deterministic --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A deterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check --keep-failed 2> $TEST_ROOT/log
if grepQuiet 'may not be deterministic' $TEST_ROOT/log; then false; fi
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
# the differences in both outputs should be reported
[[ $(grep -c 'differs' $TEST_ROOT/log) = 2 ]]
[ "$status" = "104" ]
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check --keep-failed 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
[ "$status" = "104" ]
if checkBuildTempDirRemoved $TEST_ROOT/log; then false; fi

clearStore

path=$(nix-build check.nix -A fetchurl --no-out-link)

chmod +w $path
echo foo > $path
chmod -w $path

nix-build check.nix -A fetchurl --no-out-link --check
# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat $path) = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair
[[ $(cat $path) != foo ]]

echo 'Hello World' > $TEST_ROOT/dummy
nix-build check.nix -A hashmismatch --no-out-link 2>mismatch-output || status=$?
[ "$status" = "102" ]
obtained=$(grep "got path:" mismatch-output | awk '{ print $3 }')
# The path that actually came out should exist
[ -e "$obtained" ]
# and be registered as valid
nix-store -q --references "$obtained" >/dev/null

echo -n > $TEST_ROOT/dummy
successful_fod=$(nix-build check.nix -A hashmismatch --no-out-link)

echo 'Hello World 2' > $TEST_ROOT/dummy
nix-build check.nix -A hashmismatch --no-out-link --check 2>mismatch-output || status=$?
cat mismatch-output >&2
[ "$status" = "102" ]
grep -E "expected path:\s+$successful_fod" mismatch-output
obtained=$(grep "got path:" mismatch-output | awk '{ print $3 }')
# The path that actually came out should exist
[ -e "$obtained" ]
# and be registered as valid
nix-store -q --references "$obtained" >/dev/null

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going || status=$?
[ "$status" = "110" ]
