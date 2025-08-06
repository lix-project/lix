source common.sh

clearStore

nix-build ./import-derivation.nix \
  --no-out-link \
  --option warn-import-from-derivation true \
  2>&1 | grepQuiet "warning: building '.*' during evaluation due to the use of import from derivation"
