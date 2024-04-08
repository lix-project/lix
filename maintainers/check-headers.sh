#!/usr/bin/env bash

set -eu

# n.b. this might be printed multiple times if any violating header files are
# in different parallelism groups inside pre-commit. We cannot do anything about
# this.
explanation=$(cat <<'EOF'

We found some header files that don't conform to the style guide.

The Lix style guide requests that header files:
- Begin with `#pragma once` so they only get parsed once
- Contain a doxygen comment (`/**` or `///`) containing `@file`, for
  example, `///@file`, which will make doxygen generate docs for them.

  When adding that, consider also adding a `@brief` with a sentence
  explaining what the header is for.

For more details: https://wiki.lix.systems/link/3#bkmrk-header-files
EOF
)

check_file() {
    grep -q "$1" "$2" || (echo "Missing pattern $1 in file $2" >&2; return 1)
}

patterns=(
    # makes a file get included only once even if it is included multiple times
    '^#pragma once$'
    # as used in ///@file, makes the file appear to doxygen
    '@file'
)
fail=0

for pattern in "${patterns[@]}"; do
    for file in "$@"; do
        check_file "$pattern" "$file" || fail=1
    done
done

if [[ $fail != 0 ]]; then
    echo "$explanation" >&2
    exit 1
else
    echo OK
fi
