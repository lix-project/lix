#!/usr/bin/env bash

set -euo pipefail
shopt -s inherit_errexit failglob

nixpkgss=(
    "$(nix eval --impure --raw --expr '(import ./flake.nix).inputs.nixpkgs.url')"
    "github:NixOS/nixpkgs/nixos-unstable-small"
)
jobs=(
    $(nix eval \
        --json --apply '
          let f = n: t:
            if builtins.isAttrs t
            then (if t.type or "" == "derivation"
                  then [ n ]
                  else builtins.concatMap (m: f "${n}.${m}" t.${m}) (builtins.attrNames t))
            else [];
          in f ".#.releaseTests"
        ' \
        '.#.releaseTests' \
        | jq -r '.[]'
    )
)

for override in "${nixpkgss}"
do
    (
        set -x
        nix build \
            --log-format multiline \
            --no-link \
            --override-input nixpkgs "$override" \
            "${jobs[@]}"
    )
done
