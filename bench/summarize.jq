#!/usr/bin/env -S jq -Mrf

def round3:
    . * 1000 | round | . / 1000
    ;

def stats($first):
    [
        "  mean:     \(.mean | round3)s ± \(.stddev | round3)s",
        "            user: \(.user | round3)s | system: \(.system | round3)s",
        "  median:   \(.median | round3)s",
        "  range:    \(.min | round3)s ... \(.max | round3)s",
        "  relative: \(.mean / $first.mean | round3)"
    ]
    | join("\n")
    ;

def fmt($first):
    "\(.command)\n" + (. | stats($first))
    ;

[.results | .[0] as $first | .[] | fmt($first)] | join("\n\n") | (. + "\n\n---\n")
