source common.sh

clearStore

testStdinHeredoc=$(nix eval -f - <<EOF
{
  bar = 3 + 1;
  foo = 2 + 2;
}
EOF
)
[[ $testStdinHeredoc == '{ bar = 4; foo = 4; }' ]]

nix eval --expr 'assert 1 + 2 == 3; true'
nix eval -E 'assert 1 + 2 == 3; true'

[[ $(nix eval int -f "./eval.nix") == 123 ]]
[[ $(nix eval str -f "./eval.nix") == '"foo\nbar"' ]]
[[ $(nix eval str --raw -f "./eval.nix") == $'foo\nbar' ]]
[[ "$(nix eval attr -f "./eval.nix")" == '{ foo = "bar"; }' ]]
[[ $(nix eval attr --json -f "./eval.nix") == '{"foo":"bar"}' ]]
[[ $(nix eval int -f - < "./eval.nix") == 123 ]]
[[ "$(nix eval --expr '{"assert"=1;bar=2;}')" == '{ "assert" = 1; bar = 2; }' ]]

# Non-coercible values throws errors under `--raw`
topLevelInteger="$(expectStderr 1 nix eval int --raw -f "./eval.nix")"
[[ "$topLevelInteger" =~ "error: cannot coerce an integer to a string: 123" ]]

# Top-level eval errors should be printed to stderr with a traceback.
topLevelThrow="$(expectStderr 1 nix eval --expr 'throw "a sample throw message"')"
[[ "$topLevelThrow" =~ "a sample throw message" ]]
[[ "$topLevelThrow" =~ "caused by explicit throw" ]]

# But errors inside something should print an elided version, and exit with 0.
outputOfNestedThrow="$(nix eval --expr '{ throws = throw "a sample throw message"; }')"
[[ "${outputOfNestedThrow}" == "{ throws = «error: a sample throw message»; }" ]]

# Check if toFile can be utilized during restricted eval
[[ $(nix eval --restrict-eval --expr 'import (builtins.toFile "source" "42")') == 42 ]]

nix-instantiate --eval -E 'assert 1 + 2 == 3; true'
[[ $(nix-instantiate -A int --eval "./eval.nix") == 123 ]]
[[ $(nix-instantiate -A str --eval "./eval.nix") == '"foo\nbar"' ]]
[[ $(nix-instantiate -A str --raw --eval "./eval.nix") == $'foo\nbar' ]]
[[ "$(nix-instantiate -A attr --eval "./eval.nix")" == '{ foo = "bar"; }' ]]
[[ $(nix-instantiate -A attr --eval --json "./eval.nix") == '{"foo":"bar"}' ]]
[[ $(nix-instantiate -A int --eval - < "./eval.nix") == 123 ]]
[[ "$(nix-instantiate --eval -E '{"assert"=1;bar=2;}')" == '{ "assert" = 1; bar = 2; }' ]]

# Non-coercible values throws errors under `--raw`
topLevelInteger="$(expectStderr 1 nix-instantiate -A int --raw "./eval.nix")"
[[ "$topLevelInteger" =~ "error: expression was expected to be a derivation or collection of derivations, but instead was an integer" ]]

# Check that symlink cycles don't cause a hang.
ln -sfn cycle.nix $TEST_ROOT/cycle.nix
(! nix eval --file $TEST_ROOT/cycle.nix)

# Check that relative symlinks are resolved correctly.
mkdir -p $TEST_ROOT/xyzzy $TEST_ROOT/foo
ln -sfn ../xyzzy $TEST_ROOT/foo/bar
printf 123 > $TEST_ROOT/xyzzy/default.nix
[[ $(nix eval --impure --expr "import $TEST_ROOT/foo/bar") = 123 ]]

# Test that unknown settings are warned about
out="$(expectStderr 0 nix eval --option foobar baz --expr '""' --raw)"
[[ "$(echo "$out" | grep foobar | wc -l)" = 1 ]]
