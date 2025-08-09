source common.sh

clearStore

(! nix-build check-outputs.nix -A cycle) 2>&1 | grepQuiet "cycle detected in build of '.*' in the references of output 'bar' from output 'foo'"

error="$(! nix-build check-outputs.nix -A as_dependency 2>&1)"

grepQuiet "cycle detected in build of '.*' in the references of output 'bar' from output 'foo'" <<<"$error"
grepQuiet "error: 1 dependencies of derivation" <<<"$error"
