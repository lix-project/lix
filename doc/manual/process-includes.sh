#!/bin/sh

set -euo pipefail

# re-implement mdBook's include directive to make it usable for terminal output and for proper @docroot@ substitution
(grep '{{#include' "$1" || true) | while read -r line; do
    filename="$(dirname "$1")/$(printf "$line" | sed 's/{{#include \(.*\)}}/\1/')"
    test -f "$filename" || ( echo "#include-d file '$filename' does not exist." >&2; exit 1; )
    matchline="$(printf "$line" | sed 's|/|\\/|g')"
    sed -i "/$matchline/r $filename" "$2"
    sed -i "s/$matchline//" "$2"
done
