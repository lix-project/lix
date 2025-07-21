source common.sh

clearStore

# Ensure "fake ssh" remote store works just as legacy fake ssh would.
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store doctor

# Ensure that store ping trusted works with ssh-ng://
nix --store ssh-ng://localhost?remote-store=$TEST_ROOT/other-store store ping --json | jq -e '.trusted'

startDaemon

# Ensure that ping works trusted with new daemon
nix store ping --json | jq -e '.trusted'
# Suppress grumpiness about multiple nixes on PATH
(nix doctor || true) 2>&1 | grep 'You are trusted by'

# Test import-from-derivation through the daemon.
[[ $(nix eval --impure --raw --file ./ifd.nix) = hi ]]

storeCleared=1 NIX_REMOTE_=$NIX_REMOTE $SHELL ./user-envs.sh

nix-store --gc --max-freed 1K

nix-store --dump-db > $TEST_ROOT/d1
NIX_REMOTE= nix-store --dump-db > $TEST_ROOT/d2
cmp $TEST_ROOT/d1 $TEST_ROOT/d2

# check that proxying one daemon through another works
nix --store 'ssh-ng://localhost?remote-store=ssh-ng%3a%2f%2flocalhost' store add-file ./ifd.nix

killDaemon
