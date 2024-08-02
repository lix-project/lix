source common.sh
requireGit
clearStore

container="$TEST_HOME"/flake-container
flake_dir="$container"/flake-dir

createGitRepo "$container"
mkdir -p "$flake_dir"
writeSimpleFlake "$flake_dir"
git -C "$container" add flake-dir

pushd "$flake_dir" &>/dev/null
    info="$(nix flake info --json)"
    [[ "$(jq -r '.resolvedUrl' <<<"$info")" == git+file://*/flake-container?dir=flake-dir ]]
    [[ "$(jq -r '.url' <<<"$info")" == git+file://*/flake-container?dir=flake-dir ]]

    # Make sure we can actually access & build stuff in this flake.
    nix build "path:$flake_dir#foo" -L
popd &>/dev/null
