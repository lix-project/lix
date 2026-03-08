#!/usr/bin/env bash

set -euo pipefail

function eprintln()
{
    printf "%s\n" "$@" >&2
}

ANSI_YELLOW=$'\x1b[33m'
ANSI_RESET=$'\x1b[0m'

function printUsage()
{
    echo "usage: upgrade-lix.sh <latest-tag>"
    echo
    echo "e.g.: ./upgrade-lix.sh 2.95"
}

function checkSystemdService()
{
    # Check if we have systemd.
    local SYSTEMCTL
    if command -v systemctl >/dev/null 2>&1; then
        SYSTEMCTL="$(command -v systemctl)"
    else
        # Try /usr/bin, in case PATH is on vacation.
        if [[ -x /usr/bin/systemctl ]]; then
            SYSTEMCTL="/usr/bin/systemctl"
        else
            # If there's really no systemctl, then this is probably not a systemd system.
            # On non-systemd systems, there's no issue.
            # Exit with success.
            return 0
        fi
    fi

    if "$SYSTEMCTL" cat nix-daemon@.service --no-pager >/dev/null 2>&1; then
        # The instanced daemon service unit exists, so we're good.
        return 0
    fi

    # Juuuust in case something unusual went wrong with the systemctl call,
    # we'll do one more check. This could maybe happen if someone put the file
    # in place but hasn't `daemon-reload`ed yet. Maybe.
    if [[ -e /etc/systemd/system/nix-daemon@.service ]]; then
        return 0
    fi

    # If we got here then we DO have systemd but we do NOT have nix-daemon@.service.
    # AKA: the bad case.
    eprintln "${ANSI_YELLOW}WARNING${ANSI_RESET}: manual intervention will be required to complete this Lix install!"
    eprintln "The Lix daemon is now an instanced unit that is fully socket-activated."
    eprintln
    eprintln "This requires a new systemd unit: nix-daemon@.service"
    eprintln
    eprintln "'nix upgrade-nix' will provide a profile with a new Lix, likely one of:"
    eprintln "  /nix/var/nix/profiles/default"
    eprintln "  /nix/var/nix/profiles/per-user/root/profile"
    eprintln "It will have the instanced systemd unit file at 'lib/systemd/system/nix-daemon@.service'"
    eprintln
    eprintln "You should symlink or copy this file to '/etc/systemd/system/nix-daemon@.service',"
    eprintln "and then run 'systemctl daemon-reload && systemctl restart nix-daemon.socket'."
    eprintln "Until you do that, all nix commands will require root and '--store local'."
    eprintln

    local RESPONSE
    # Single-character read with a prompt, for a traditional y/n prompt.
    read -er -N 1 -p "Procede with installation? [y/N] " RESPONSE

    # Lowercase the respose before comparing: Y and y are both "yes".
    # Anything else is "no".
    if [[ "${RESPONSE@L}" = "y" ]]; then
        return 0
    fi

    eprintln "Installation cancelled!"
    exit 1
}

# We take only one argument.
# If that argument looks "help"-y, then we should print usage instead.
if [[ "$#" -eq 1 ]]; then
    case "$1" in
        -h|-H|-?|*help*)
            printUsage
            exit 0
        ;;
    esac
else
    printUsage
    exit 0
fi

LIX_VERSION="$1"
# Let people override these if they reeeeeeally need to...
LIX_REF="${LIX_REF:-refs/tags/$LIX_VERSION}"
LIX_BASE="${LIX_BASE:-"git+https://git.lix.systems/lix-project/lix"}"

if [[ "$EUID" != 0 ]]; then
    eprintln "Please run this script as root."
    eprintln "If you use sudo, make sure your PATH environment variable is kept with '--preserve-env=PATH'."
    exit 2
fi

if ! command -v nix >/dev/null 2>&1; then
    eprintln "Couldn't find a 'nix' in your PATH: '$PATH'"
    eprintln "If you're running with sudo, make sure PATH is preserved with '--preserve-env=PATH'."
    eprintln "If you're passing '--preserve-env=PATH', check /etc/sudoers for 'env_reset' and 'env_keep'."
    eprintln "If you cannot modify that, run this script from a root shell, instead of directly from sudo."
    exit 3
fi

NIX="${NIX:-$(command -v nix)}"

declare -a SUBSTITUTER_ARGS=(
    "--extra-substituters"
    "https://cache.lix.systems"
    "--extra-trusted-public-keys"
    "cache.lix.systems:aBnZUw8zA7H35Cz2RyKFVs3H4PlGTLawyY5KRbvJR8o="
)

checkSystemdService

set -x
exec "$NIX" run \
    --extra-experimental-features "nix-command flakes" \
    "${SUBSTITUTER_ARGS[@]}" \
    "${LIX_BASE}?ref=${LIX_REF}" \
    --extra-experimental-features "nix-command flakes" \
    upgrade-nix \
    "${SUBSTITUTER_ARGS[@]}"
