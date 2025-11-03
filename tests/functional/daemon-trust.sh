source common.sh

clearStore

effectiveUser() {
    whoami || id -u
}

withDaemonTrusting() {
    local trusting="$1"
    shift

    trap killDaemon EXIT

    NIX_CONFIG="
extra-experimental-features = daemon-trust-override
trusted-users = $trusting
" startDaemon "$@"
}

(
    withDaemonTrusting "" --default-trust
    nix store ping --json | jq -e '.trusted | not'
)
(
    withDaemonTrusting "" --force-trusted
    nix store ping --json | jq -e '.trusted'
)

(
    withDaemonTrusting "$(effectiveUser)" --default-trust
    nix store ping --json | jq -e '.trusted'
)
(
    withDaemonTrusting "$(effectiveUser)" --force-untrusted
    nix store ping --json | jq -e '.trusted | not'
)
