#!/bin/sh

set -euo pipefail

unescape_dashes=

if [ "$1" = --unescape-dashes ]; then
    unescape_dashes=yes
    shift
fi

title="$1"
section="$2"
infile="$3"
tmpfile="$4"
outfile="$5"

printf "Title: %s\n\n" "$title" > "$tmpfile"
cat "$infile" >> "$tmpfile"
"$(dirname "$0")"/process-includes.sh "$infile" "$tmpfile"
lowdown -sT man --nroff-nolinks -M section="$section" "$tmpfile" -o "$outfile"
if [ -n "$unescape_dashes" ]; then
    # fix up `lowdown`'s automatic escaping of `--`
    # https://github.com/kristapsdz/lowdown/blob/edca6ce6d5336efb147321a43c47a698de41bb7c/entity.c#L202
    sed -i 's/\e\[u2013\]/--/' "$outfile"
fi
rm "$tmpfile"
