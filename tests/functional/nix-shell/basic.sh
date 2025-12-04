source ../common.sh

clearStore

if [[ -n ${CONTENT_ADDRESSED:-} ]]; then
    shellDotNix="$PWD/ca-shell.nix"
else
    shellDotNix="$PWD/shell.nix"
fi

export NIX_PATH=nixpkgs="$shellDotNix"

# Test nix-shell -A
export IMPURE_VAR=foo
export SELECTED_IMPURE_VAR=baz

output=$(nix-shell --pure "$shellDotNix" -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"')

[ "$output" = " - foo - bar - true" ]

# Test --keep
output=$(nix-shell --pure --keep SELECTED_IMPURE_VAR "$shellDotNix" -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $SELECTED_IMPURE_VAR"')

[ "$output" = " - foo - bar - baz" ]

# Test nix-shell on a .drv
[[ $(nix-shell --pure $(nix-instantiate "$shellDotNix" -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

[[ $(nix-shell --pure $(nix-instantiate "$shellDotNix" -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

# Test nix-shell on a .drv symlink

# Legacy: absolute path and .drv extension required
nix-instantiate "$shellDotNix" -A shellDrv --add-root $TEST_ROOT/shell.drv
[[ $(nix-shell --pure $TEST_ROOT/shell.drv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# New behaviour: just needs to resolve to a derivation in the store
nix-instantiate "$shellDotNix" -A shellDrv --add-root $TEST_ROOT/shell
[[ $(nix-shell --pure $TEST_ROOT/shell --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# Test nix-shell -p
output=$(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = "foo bar" ]

# Test nix-shell -p --arg x y
output=$(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo --argstr fooContents baz --run 'echo "$(foo)"')
[ "$output" = "baz" ]

# Test nix-shell returns the valid exit codes
[[ $(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo --run 'false'; echo $?) -eq 1 ]] && echo "nix-shell correctly propagated the exit code of 'false'" || { echo "nix-shell did not propagate the exit code of 'false'"; exit 1; }

# Test that nix-shell have a valid $TEMPDIR and $NIX_BUILD_TOP when $TMPDIR is unset.
# We will make this test up to one nested nix shells.
# NOTE: making it go to two nested nix shell is left as an exercise in Bash fun.
(
    unset TMPDIR
    export NIX_PATH=nixpkgs="$shellDotNix"
    nix-shell -p foo --run '
      # Outer nix-shell validity check
      [[ -n "$NIX_BUILD_TOP" && -d "$NIX_BUILD_TOP" && -n "$TEMPDIR" && -d "$TEMPDIR" ]] && echo "Outer nix-shell has a valid \$TEMPDIR and \$NIX_BUILD_TOP" || { echo "Outer nix-shell does not have a valid \$TEMPDIR or \$NIX_BUILD_TOP"; exit 1; }
    ' || exit 1
)

# Test that nix-shell deletes the parent of $TEMPDIR which is the nix-shell directory
# when $TMPDIR is unset.
(
    unset TMPDIR
    IN_SHELL_TEMPDIR=$(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo --run 'echo $TEMPDIR')
    test ! -d "$(dirname $IN_SHELL_TEMPDIR)" && echo "nix-shell deleted the parent directory of \$TEMPDIR: clean up successful" || { echo "nix-shell did not delete the parent directory of \$TEMPDIR: clean up failure"; exit 1; }
)

# Test whether the added length w.r.t. base directories is reasonable enough.
# This prevents regressions that can affect negatively things like opening UNIX
# domain sockets in non-isolated builds.
(
    unset TMPDIR
    BASE_DIR="$(realpath /tmp)"

    LENGTH_RESULT=$(
      NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo --run "
        BASE_DIR_ENV=\"$BASE_DIR\"
        ADDED_LENGTH=\$(( \${#NIX_BUILD_TOP} - \${#BASE_DIR_ENV} ))
        echo \"\$ADDED_LENGTH:\$NIX_BUILD_TOP\"
      "
    )

    ADDED_LENGTH="${LENGTH_RESULT%%:*}"
    IN_SHELL_NIX_BUILD_TOP="${LENGTH_RESULT#*:}"

    # The idea of this value is len("nix-shell-$hash/build-top") + ≤5 chars of margin.
    MAX_ALLOWED=50
    # The added length must be minimum 2 chars.
    MIN_ALLOWED=2

    if (( ADDED_LENGTH < MIN_ALLOWED )); then
        echo "Added length ($ADDED_LENGTH) is impossibly low. The test is not correct."
        exit 1
    fi

    if (( ADDED_LENGTH <= MAX_ALLOWED )); then
        echo "Added length to \$NIX_BUILD_TOP in shells ($ADDED_LENGTH chars) is within acceptable limit ($MAX_ALLOWED chars)."
    else
        echo "ERROR: Added length too large ($ADDED_LENGTH chars > $MAX_ALLOWED chars)."
        echo "Base: $BASE_DIR"
        echo "NIX_BUILD_TOP: $IN_SHELL_NIX_BUILD_TOP"
        exit 1
    fi
)

# Test $NIX_LOG_FD
expectStderr 0 nix-shell --pure $shellDotNix -A shellDrv --run 'echo hello > $NIX_LOG_FD' |& grepQuiet 'hello'

# FIXME: testing that Ctrl-C to a (non-)interactive nix-shell stops it would be appreciated,
# but it is hard to send accurately the signal to the right process group.

# Test nix-shell shebang mode
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.sh > $TEST_ROOT/shell.shebang.sh
chmod a+rx $TEST_ROOT/shell.shebang.sh

output=$($TEST_ROOT/shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]

# Test nix-shell shebang mode again with metacharacters in the filename.
# First word of filename is chosen to not match any file in the test root.
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.sh > $TEST_ROOT/spaced\ \\\'\"shell.shebang.sh
chmod a+rx $TEST_ROOT/spaced\ \\\'\"shell.shebang.sh

output=$($TEST_ROOT/spaced\ \\\'\"shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]

# Test nix-shell shebang mode for ruby
# This uses a fake interpreter that returns the arguments passed
# This, in turn, verifies the `rc` script is valid and the `load()` script (given using `-e`) is as expected.
sed -e "s|@SHELL_PROG@|$(type -P nix-shell)|" shell.shebang.rb > $TEST_ROOT/shell.shebang.rb
chmod a+rx $TEST_ROOT/shell.shebang.rb

output=$($TEST_ROOT/shell.shebang.rb abc ruby)
[ "$output" = '-e load(ARGV.shift) -- '"$TEST_ROOT"'/shell.shebang.rb abc ruby' ]

# Test nix-shell shebang mode for ruby again with metacharacters in the filename.
# Note: fake interpreter only space-separates args without adding escapes to its output.
sed -e "s|@SHELL_PROG@|$(type -P nix-shell)|" shell.shebang.rb > $TEST_ROOT/spaced\ \\\'\"shell.shebang.rb
chmod a+rx $TEST_ROOT/spaced\ \\\'\"shell.shebang.rb

output=$($TEST_ROOT/spaced\ \\\'\"shell.shebang.rb abc ruby)
[ "$output" = '-e load(ARGV.shift) -- '"$TEST_ROOT"'/spaced \'\''"shell.shebang.rb abc ruby' ]

# Test nix-shell shebang quoting
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.nix > $TEST_ROOT/shell.shebang.nix
chmod a+rx $TEST_ROOT/shell.shebang.nix
$TEST_ROOT/shell.shebang.nix

# Test 'nix develop'.
nix develop -f "$shellDotNix" shellDrv -c bash -c '[[ -n $stdenv ]]'

# Ensure `nix develop -c` preserves stdin
echo foo | nix develop -f "$shellDotNix" shellDrv -c cat | grepQuiet foo

# Ensure `nix develop -c` actually executes the command if stdout isn't a terminal
nix develop -f "$shellDotNix" shellDrv -c echo foo |& grepQuiet foo

# Test 'nix print-dev-env'.

nix print-dev-env -f "$shellDotNix" shellDrv > $TEST_ROOT/dev-env.sh
nix print-dev-env -f "$shellDotNix" shellDrv --json > $TEST_ROOT/dev-env.json

# Test with raw drv

shellDrv=$(nix-instantiate "$shellDotNix" -A shellDrv.out)

nix develop $shellDrv -c bash -c '[[ -n $stdenv ]]'

# Test $NIX_LOG_FD
expectStderr 0 nix develop $shellDrv -c bash -c 'echo hello > $NIX_LOG_FD' |& grepQuiet 'hello'

nix print-dev-env $shellDrv > $TEST_ROOT/dev-env2.sh
nix print-dev-env $shellDrv --json > $TEST_ROOT/dev-env2.json

diff $TEST_ROOT/dev-env{,2}.sh
diff $TEST_ROOT/dev-env{,2}.json

# Ensure `nix print-dev-env --json` contains variable assignments.
[[ $(jq -r .variables.arr1.value[2] $TEST_ROOT/dev-env.json) = '3 4' ]]

# Run tests involving `source <(nix print-dev-env)` in subshells to avoid modifying the current
# environment.

set -u

# Ensure `source <(nix print-dev-env)` modifies the environment.
(
    path=$PATH
    source $TEST_ROOT/dev-env.sh
    [[ -n $stdenv ]]
    [[ ${arr1[2]} = "3 4" ]]
    [[ ${arr2[1]} = $'\n' ]]
    [[ ${arr2[2]} = $'x\ny' ]]
    [[ $(fun) = blabla ]]
    [[ "$ASCII_ESC" = "$(printf "\e")" ]]
    [[ $PATH = $(jq -r .variables.PATH.value $TEST_ROOT/dev-env.json):$path ]]
)

# Ensure `source <(nix print-dev-env)` handles the case when PATH is empty.
(
    path=$PATH
    PATH=
    source $TEST_ROOT/dev-env.sh
    [[ $PATH = $(PATH=$path jq -r .variables.PATH.value $TEST_ROOT/dev-env.json) ]]
)

# Test nix-shell with ellipsis and no `inNixShell` argument (for backwards compat with old nixpkgs)
cat >$TEST_ROOT/shell-ellipsis.nix <<EOF
{ system ? "x86_64-linux", ... }@args:
assert (!(args ? inNixShell));
(import $shellDotNix { }).shellDrv
EOF
nix-shell $TEST_ROOT/shell-ellipsis.nix --run "true"
