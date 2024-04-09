#!/usr/bin/env bash

set -euo pipefail
shopt -s inherit_errexit

scriptdir=$(cd "$(dirname -- "$0")" ; pwd -P)
cd "$scriptdir/.."

if [[ $# -lt 2 ]]; then
    # FIXME(jade): it is a reasonable use case to want to run a benchmark run
    # on just one build. However, since we are using hyperfine in comparison
    # mode, we would have to combine the JSON ourselves to support that, which
    # would probably be better done by writing a benchmarking script in
    # not-bash.
    echo "Fewer than two result dirs given, nothing to compare!" >&2
    echo "Pass some directories (with names indicating which alternative they are) with bin/nix in them" >&2
    echo "Usage: ./bench/bench.sh result-1 result-2 [result-3...]" >&2
    exit 1
fi

_exit=""
trap "$_exit" EXIT

# XXX: yes this is very silly. flakes~!!
nix build --impure --expr '(builtins.getFlake "git+file:.").inputs.nixpkgs.outPath' -o bench/nixpkgs

export NIX_REMOTE="$(mktemp -d)"
_exit='rm -rfv "$NIX_REMOTE"; $_exit'
export NIX_PATH="nixpkgs=bench/nixpkgs:nixos-config=bench/configuration.nix"

builds=("$@")

flake_args="--extra-experimental-features 'nix-command flakes'"

hyperfineArgs=(
    --parameter-list BUILD "$(IFS=,; echo "${builds[*]}")"
    --warmup 2 --runs 10
)

declare -A cases
cases=(
    [search]="{BUILD}/bin/nix $flake_args search --no-eval-cache github:nixos/nixpkgs/e1fa12d4f6c6fe19ccb59cac54b5b3f25e160870 hello"
    [rebuild]="{BUILD}/bin/nix $flake_args eval --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'"
    [rebuild-lh]="GC_INITIAL_HEAP_SIZE=10g {BUILD}/bin/nix eval $flake_args --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'"
    [parse]="{BUILD}/bin/nix $flake_args eval -f bench/nixpkgs/pkgs/development/haskell-modules/hackage-packages.nix"
)

benches=(
    rebuild
    rebuild-lh
    search
    parse
)

for k in "${benches[@]}"; do
    taskset -c 2,3 \
        chrt -f 50 \
        hyperfine "${hyperfineArgs[@]}"  --export-json="bench/bench-${k}.json" --export-markdown="bench/bench-${k}.md" "${cases[$k]}"
done

echo "Benchmarks summary (from ./bench/summarize.jq bench/bench-*.json)"
bench/summarize.jq bench/*.json
