source ../common.sh

clearStore

export NIX_BUILD_SHELL=$SHELL
env NIX_PATH=nixpkgs=shell.nix nix-shell structured-attrs-shell.nix \
    --run 'test "3" = "$(jq ".my.list|length" < $NIX_ATTRS_JSON_FILE)"'

nix develop -f structured-attrs-shell.nix -c bash -c 'test "3" = "$(jq ".my.list|length" < $NIX_ATTRS_JSON_FILE)"'

# `nix develop` is a slightly special way of dealing with environment vars, it parses
# these from a shell-file exported from a derivation. This is to test especially `outputs`
# (which is an associative array in thsi case) being fine.
nix develop -f structured-attrs-shell.nix -c bash -c 'test -n "$out"'

nix print-dev-env -f structured-attrs-shell.nix | grepQuiet 'NIX_ATTRS_JSON_FILE='
nix print-dev-env -f structured-attrs-shell.nix | grepQuiet 'NIX_ATTRS_SH_FILE='
nix print-dev-env -f shell.nix shellDrv | grepQuietInverse 'NIX_ATTRS_SH_FILE'

jsonOut="$(nix print-dev-env -f structured-attrs-shell.nix --json)"

test "$(<<<"$jsonOut" jq '.structuredAttrs|keys|.[]' -r)" = "$(printf ".attrs.json\n.attrs.sh")"

test "$(<<<"$jsonOut" jq '.variables.out.value' -r)" = "$(<<<"$jsonOut" jq '.structuredAttrs.".attrs.json"' -r | jq -r '.outputs.out')"
