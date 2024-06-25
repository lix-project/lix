#!/usr/bin/env bash

exec nix-daemon --force-untrusted "$@"
