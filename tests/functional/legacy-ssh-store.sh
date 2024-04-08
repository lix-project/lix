source common.sh

store_uri="ssh://localhost?remote-store=$TEST_ROOT/other-store"

# Check that store ping trusted doesn't yet work with ssh://
nix --store "$store_uri" store ping --json | jq -e 'has("trusted") | not'

# Suppress grumpiness about multiple nixes on PATH
(nix --store "$store_uri" doctor || true) 2>&1 | grep 'You are unknown trust'
