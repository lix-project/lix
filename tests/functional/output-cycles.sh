source common.sh

clearStore

error="$(! nix-build check-outputs.nix -A cycle 2>&1)"
grepQuiet "cycle detected in build of '.*' in the references of output 'bar' from output 'foo'" <<<"$error"

if [[ "$(uname -s)" = Linux ]]; then
    <<<"$error" grepQuiet "/store/.*-cycle-bar"
    <<<"$error" grepQuiet "└───lib/libfoo: ….*cycle-baz.*"
    <<<"$error" grepQuiet "    →.*/store/.*-cycle-baz"
    <<<"$error" grepQuiet "    └───share/lalala:.*-cycle-foo.*"
fi

error="$(! nix-build check-outputs.nix -A as_dependency 2>&1)"

grepQuiet "cycle detected in build of '.*' in the references of output 'bar' from output 'foo'" <<<"$error"
grepQuiet "error: 1 dependencies of derivation" <<<"$error"
