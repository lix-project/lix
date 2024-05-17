{ lib, runCommand, shouldBePresent ? false }:

runCommand "core-dump-now" { } ''
  set -m
  sleep infinity &

  # make a coredump
  kill -SIGSEGV %1

  if ${lib.optionalString (shouldBePresent) "!"} test -n "$(find . -maxdepth 1 -name 'core*' -print -quit)"; then
    echo "core file was in wrong presence state, expected: ${if shouldBePresent then "present" else "missing"}"
    exit 1
  fi

  touch $out
''
