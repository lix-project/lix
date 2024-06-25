#!/usr/bin/env bash

set -euo pipefail

infile="$1"
outfile="$2"
shift 2

# set a search path for includes. the old makefile-based system splorked
# everything into the source tree and was thus able to not have a search
# path, but the meson system generates intermediate files into dedicated
# directories separate from the source. we still retain the implicit old
# behavior for now as the base search path, once meson is the default we
# can revisit this and remove the implicit search path entry. it's fine.
set -- "$(dirname "$infile")" "$@"

# re-implement mdBook's include directive to make it usable for terminal output and for proper @docroot@ substitution
(grep '{{#include' "$infile" || true) | while read -r line; do
    found=false
    include="$(printf "$line" | sed 's/{{#include \(.*\)}}/\1/')"
    for path in "$@"; do
        filename="$path/$include"
        if [ -e "$filename" ]; then
            found=true
            matchline="$(printf "$line" | sed 's|/|\\/|g')"
            sed -i "/$matchline/r $filename" "$outfile"
            sed -i "s/$matchline//" "$outfile"
            break
        fi
    done
    $found || ( echo "#include-d file '$filename' does not exist." >&2; exit 1; )
done
