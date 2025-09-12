source common.sh

testDir="$PWD"
cd "$TEST_ROOT"

replCmds="
simple = 1
simple = import $testDir/simple.nix
:bl simple
:log simple
"

replFailingCmds="
failing = import $testDir/simple-failing.nix
:b failing
:log failing
"

replUndefinedVariable="
import $testDir/undefined-variable.nix
"

testRepl () {
    local nixArgs=("$@")
    rm -rf repl-result-out || true # cleanup from other runs backed by a foreign nix store
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replCmds")"
    echo "$replOutput"
    local outPath=$(echo "$replOutput" |&
        grep -o -E "$NIX_STORE_DIR/\w*-simple")
    nix path-info "${nixArgs[@]}" "$outPath"
    [ "$(realpath ./repl-result-out)" == "$outPath" ] || fail "nix repl :bl doesn't make a symlink"
    # run it again without checking the output to ensure the previously created symlink gets overwritten
    nix repl "${nixArgs[@]}" <<< "$replCmds" || fail "nix repl does not work twice with the same inputs"

    # simple.nix prints a PATH during build
    echo "$replOutput" | grepQuiet -s 'PATH=' || fail "nix repl :log doesn't output logs"
    local replOutput="$(nix repl "${nixArgs[@]}" <<< "$replFailingCmds" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s 'This should fail' \
      || fail "nix repl :log doesn't output logs for a failed derivation"
    local replOutput="$(nix repl --show-trace "${nixArgs[@]}" <<< "$replUndefinedVariable" 2>&1)"
    echo "$replOutput"
    echo "$replOutput" | grepQuiet -s "while evaluating the file" \
      || fail "nix repl --show-trace doesn't show the trace"

    nix repl "${nixArgs[@]}" --option pure-eval true 2>&1 <<< "builtins.currentSystem" \
      | grep "attribute 'currentSystem' missing"
    nix repl "${nixArgs[@]}" 2>&1 <<< "builtins.currentSystem" \
      | grep "$(nix-instantiate --eval -E 'builtins.currentSystem')"

    # . is TEST_ROOT
    cp "$testDir/simple.nix" .
    local replOutput="$(nix repl "${nixArgs[@]}" ./simple.nix 2>&1)"
    echo "$replOutput"
    echo "$replOutput" \
      | grepQuiet "error: could not find a flake.nix file" \
      || fail "nix repl simple.nix doesn't fail because simple.nix is not a flake"
}

# Simple test, try building a drv
testRepl
# Same thing (kind-of), but with a remote store.
testRepl --store "$TEST_ROOT/store?real=$NIX_STORE_DIR"

# Remove ANSI escape sequences. They can prevent grep from finding a match.
stripColors () {
    sed -E 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g'
}

testReplResponseGeneral () {
    local grepMode="$1"; shift
    local commands="$1"; shift
    # Expected response can contain newlines.
    # grep can't handle multiline patterns, so replace newlines with TEST_NEWLINE
    # in both expectedResponse and response.
    # awk ORS always adds a trailing record separator, so we strip it with sed.
    local expectedResponse="$(printf '%s' "$1" | awk 1 ORS=TEST_NEWLINE | sed 's/TEST_NEWLINE$//')"; shift
    # We don't need to strip trailing record separator here, since extra data is ok.
    local response="$(nix repl "$@" <<< "$commands" 2>&1 | stripColors | awk 1 ORS=TEST_NEWLINE)"
    printf '%s' "$response" | grepQuiet "$grepMode" -s "$expectedResponse" \
      || fail "$(echo "repl command set:

$commands

does not respond with:

---
$expectedResponse
---

but with:

---
$response
---

" | sed 's/TEST_NEWLINE/\n/g')"
}

testReplResponse () {
    testReplResponseGeneral --basic-regexp "$@"
}

testReplResponseNoRegex () {
    testReplResponseGeneral --fixed-strings "$@"
}

# :a uses the newest version of a symbol
testReplResponse '
:a { a = "1"; }
:a { a = "2"; }
"result: ${a}"
' "result: 2"

# check dollar escaping https://github.com/NixOS/nix/issues/4909
# note the escaped \,
#    \\
# because the second argument is a regex
testReplResponseNoRegex '
"$" + "{hi}"
' '"\${hi}"'

testReplResponse '
drvPath
' '".*-simple.drv"' \
--file $testDir/simple.nix

mkdir -p flake && cat <<EOF > flake/flake.nix
{
    outputs = { self }: {
        foo = 1;
        bar.baz = 2;

        changingThing = "beforeChange";
    };
}
EOF
testReplResponse '
foo + baz
' "3" \
    ./flake ./flake\#bar --experimental-features 'flakes'

# Test the `:reload` mechansim with flakes:
# - Eval `./flake#changingThing`
# - Modify the flake
# - Re-eval it
# - Check that the result has changed
replResult=$( (
echo "changingThing"
mkfifo fifo
echo "builtins.readFile ./fifo"
echo > fifo
sed -i 's/beforeChange/afterChange/' flake/flake.nix
echo ":reload"
echo "changingThing"
) | nix repl ./flake --experimental-features 'flakes')
echo "$replResult" | grepQuiet -s beforeChange
echo "$replResult" | grepQuiet -s afterChange

# Test recursive printing and formatting
# Normal output should print attributes in lexicographical order non-recursively
testReplResponseNoRegex '
{ a = { b = 2; }; l = [ 1 2 3 ]; s = "string"; n = 1234; x = rec { y = { z = { inherit y; }; }; }; }
' \
'{
  a = { ... };
  l = [ ... ];
  n = 1234;
  s = "string";
  x = { ... };
}
'

# Same for lists, but order is preserved
testReplResponseNoRegex '
[ 42 1 "thingy" ({ a = 1; }) ([ 1 2 3 ]) ]
' \
'[
  42
  1
  "thingy"
  { ... }
  [ ... ]
]
'

# Same for let expressions
testReplResponseNoRegex '
let x = { y = { a = 1; }; inherit x; }; in x
' \
'{
  x = «repeated»;
  y = { ... };
}
'

# The :p command should recursively print sets, but prevent infinite recursion
testReplResponseNoRegex '
:p { a = { b = 2; }; s = "string"; n = 1234; x = rec { y = { z = { inherit y; }; }; }; }
' \
'{
  a = { b = 2; };
  n = 1234;
  s = "string";
  x = {
    y = {
      z = {
        y = «repeated»;
      };
    };
  };
}
'

# Same for lists
testReplResponseNoRegex '
:p [ 42 1 "thingy" (rec { a = 1; b = { inherit a; inherit b; }; }) ([ 1 2 3 ]) ]
' \
'[
  42
  1
  "thingy"
  {
    a = 1;
    b = {
      a = 1;
      b = «repeated»;
    };
  }
  [
    1
    2
    3
  ]
]
'

# Same for let expressions
testReplResponseNoRegex '
:p let x = { y = { a = 1; }; inherit x; }; in x
' \
'{
  x = «repeated»;
  y = { a = 1; };
}
'

# Test that editing a store path does not reload...
echo '{ identity = a: a; }' > repl-test.nix
repl_test_store="$(nix-store --add repl-test.nix)"
EDITOR=true testReplResponseNoRegex "
a = ''test string that we'll grep later''
:l $repl_test_store
:e identity
a
" "test string that we'll grep later"

# ...even through symlinks
ln -s "$repl_test_store" repl-test-link.nix
EDITOR=true testReplResponseNoRegex "
a = ''test string that we'll grep later''
:l repl-test-link.nix
:e identity
a
" "test string that we'll grep later"

# Test that editing a local file does reload
EDITOR=true testReplResponseNoRegex "
a = ''test string that we'll grep later''
:l repl-test.nix
:e identity
a
" "undefined variable"

# Test :log with derivation paths.
simple_path="$(nix-instantiate "$testDir/simple.nix")"
# `PATH=` is a part of build log.
testReplResponseNoRegex ":log ${simple_path}" "PATH="
