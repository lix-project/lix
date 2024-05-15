#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname -- "$0")/.."

# This script uploads the Lix manual to the Lix s3 store.
# It expects credentials to be configured like so:
#
# ~/.aws/credentials:
#
# [default]
# aws_access_key_id = SOMEACCESSKEY
# aws_secret_access_key = SOMESECRETKEY
#
# default can also be replaced by some other string if AWS_PROFILE is set in
# environment.
#
# See: https://rclone.org/s3/#authentication
#
# To obtain such a key, log into the garage host and run:
# (obtain GARAGE_RPC_SECRET into environment perhaps by systemctl cat garage)
# garage key create SOME-KEY-NAME
# garage bucket allow --read --write docs --key SOME-KEY-NAME

if [[ ! -f result-doc/share/doc/nix/manual/index.html ]]; then
    echo -e "result-doc does not appear to contain a Lix manual. You can build one with:\n  nix build '.#default^*'" >&2
    exit 1
fi

# --checksum: https://rclone.org/s3/#avoiding-head-requests-to-read-the-modification-time
# By default rclone uses the modification time to determine if something needs
# syncing. This is actually very bad for our use case, since we have small
# files that have meaningless (Unix epoch) local modification time data. We can
# make it go both 16x faster and more correct by using md5s instead.
rclone \
    --config doc/manual/rclone.conf \
    -vv \
    sync \
    --checksum \
    result-doc/share/doc/nix/manual/ lix-docs:docs/manual/nightly/
