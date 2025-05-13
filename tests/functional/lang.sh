#!/usr/bin/env bash

source common.sh

set -o pipefail

cd lang
source framework.sh

# specialize function a bit
function diffAndAccept() {
    local -r testName="$1"
    local -r got="$testName.$2"
    local -r expected="$testName.$3"
    diffAndAcceptInner "$testName" "$got" "$expected"
}

export TEST_VAR=foo # for eval-okay-getenv.nix
export NIX_REMOTE=dummy://
export NIX_STORE_DIR=/nix/store

nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.trace "Hello" 123' 2>/dev/null | grepQuiet 123
nix-instantiate --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1
nix-instantiate --trace-verbose --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuiet Hello
nix-instantiate --eval -E 'builtins.traceVerbose "Hello" 123' 2>&1 | grepQuietInverse Hello
nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello" 123' 2>&1 | grepQuietInverse Hello
expectStderr 1 nix-instantiate --show-trace --eval -E 'builtins.addErrorContext "Hello %" (throw "Foo")' | grepQuiet 'Hello %'

nix-instantiate --eval -E 'let x = builtins.trace { x = x; } true; in x' \
  2>&1 | grepQuiet -E 'trace: { x = «potential infinite recursion»; }'

nix-instantiate --eval -E 'let x = { repeating = x; tracing = builtins.trace x true; }; in x.tracing'\
  2>&1 | grepQuiet -F 'trace: { repeating = «repeated»; tracing = «potential infinite recursion»; }'

set +x

badDiff=0
badExitCode=0

for i in parse-fail-*.nix; do
    echo "parsing $i (should fail)";
    i=$(basename "$i" .nix)

    declare -a flags=()
    if test -e "$i.flags"; then
        read -r -a flags < "$i.flags"
    fi
    if expectStderr 1 nix-instantiate --parse "${flags[@]}" "$i.nix" > "$i.err"
    then
        sed -i "s!$(pwd)!/pwd!g" "$i.err"
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i shouldn't parse"
        badExitCode=1
    fi
done

for i in parse-okay-*.nix; do
    echo "parsing $i (should succeed)";
    i=$(basename "$i" .nix)

    declare -a flags=()
    if test -e "$i.flags"; then
        read -r -a flags < "$i.flags"
    fi
    if
        expect 0 nix-instantiate --parse "${flags[@]}" "$i.nix" \
            1> "$i.out" \
            2> "$i.err"
    then
        sed -i "s!$(pwd)!/pwd!g" "$i.out" "$i.err"
        yq --in-place --yaml-output '.' "$i.out"
        diffAndAccept "$i" out exp
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i should parse"
        badExitCode=1
    fi
done

for i in eval-fail-*.nix; do
    echo "evaluating $i (should fail)";
    i=$(basename "$i" .nix)

    declare -a flags=()
    if test -e "$i.flags"; then
        read -r -a flags < "$i.flags"
    fi
    if
        expectStderr 1 nix-instantiate --eval --strict --show-trace "${flags[@]}" "$i.nix" \
            | sed "s!$(pwd)!/pwd!g" > "$i.err"
    then
        diffAndAccept "$i" err err.exp
    else
        echo "FAIL: $i shouldn't evaluate"
        badExitCode=1
    fi
done

for i in eval-okay-*.nix; do
    echo "evaluating $i (should succeed)";
    i=$(basename "$i" .nix)

    declare -a flags=()
    if test -e "$i.flags"; then
        read -r -a flags < "$i.flags"
    fi

    if test -e "$i.exp.xml"; then
        if expect 0 nix-instantiate --eval --xml --no-location --strict "${flags[@]}" \
                "$i.nix" > "$i.out.xml"
        then
            diffAndAccept "$i" out.xml exp.xml
        else
            echo "FAIL: $i should evaluate"
            badExitCode=1
        fi
    else
        if
            expect 0 env \
                NIX_PATH=dir3:dir4 \
                HOME=/fake-home \
                nix-instantiate "${flags[@]}" --eval --strict "$i.nix" \
                1> "$i.out" \
                2> "$i.err"
        then
            sed -i "s!$(pwd)!/pwd!g" "$i.out" "$i.err"
            diffAndAccept "$i" out exp
            diffAndAccept "$i" err err.exp
        else
            echo "FAIL: $i should evaluate"
            badExitCode=1
        fi
    fi
done

if test -n "${_NIX_TEST_ACCEPT-}"; then
    if (( "$badDiff" )); then
        echo 'Output did mot match, but accepted output as the persisted expected output.'
        echo 'That means the next time the tests are run, they should pass.'
    else
        echo 'NOTE: Environment variable _NIX_TEST_ACCEPT is defined,'
        echo 'indicating the unexpected output should be accepted as the expected output going forward,'
        echo 'but no tests had unexpected output so there was no expected output to update.'
    fi
    if (( "$badExitCode" )); then
        exit "$badExitCode"
    else
        skipTest "regenerating golden masters"
    fi
else
    if (( "$badDiff" )); then
        echo ''
        echo 'You can rerun this test with:'
        echo ''
        echo '    _NIX_TEST_ACCEPT=1 just test --suite installcheck -v functional-lang'
        echo ''
        echo 'to regenerate the files containing the expected output,'
        echo 'and then view the git diff to decide whether a change is'
        echo 'good/intentional or bad/unintentional.'
        echo 'If the diff contains arbitrary or impure information,'
        echo 'please improve the normalization that the test applies to the output.'
    fi
    exit $(( "$badExitCode" + "$badDiff" ))
fi
