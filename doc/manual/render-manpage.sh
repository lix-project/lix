#!/bin/sh

set -euo pipefail

lowdown_args=

if [ "$1" = --out-no-smarty ]; then
    lowdown_args=--out-no-smarty
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
lowdown -sT man --nroff-nolinks $lowdown_args -M section="$section" "$tmpfile" -o "$outfile"
rm "$tmpfile"
