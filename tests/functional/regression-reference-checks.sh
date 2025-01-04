source common.sh

clearStore

outpath="$(nix-build regression-reference-checks.nix -A out --no-out-link)"
manpage="$(nix-build regression-reference-checks.nix -A man --no-out-link)"

nix-store --delete "$outpath"
nix-build regression-reference-checks.nix -A out --no-out-link
