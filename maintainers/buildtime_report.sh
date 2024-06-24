#!/usr/bin/env bash

# Generates a report of build time based on a meson build using -ftime-trace in
# Clang.
if [ $# -lt 1 ]; then
    echo "usage: $0 BUILD-DIR [filename]" >&2
    exit 1
fi

scriptdir=$(cd "$(dirname -- "$0")" || exit ; pwd -P)
filename=${2:-$scriptdir/../buildtime.bin}

if [ "$(meson introspect "$1" --buildoptions | jq -r '.[]  | select(.name == "profile-build") | .value')" != enabled ]; then
    echo 'This build was not done with profile-build enabled, so cannot generate a report' >&2
    # shellcheck disable=SC2016
    echo 'Run `meson configure build -Dprofile-build=enabled` then rebuild, first' >&2
    exit 1
fi

ClangBuildAnalyzer --all "$1" "$filename" && ClangBuildAnalyzer --analyze "$filename"
