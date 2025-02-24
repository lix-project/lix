source common.sh

cd "$TEST_ROOT"

# To start off, we will produce some custom binaries that redirects to known functionality.
EXTRA_BINARIES_DIR=$(mktemp -d)
# trap "rm -rf $EXTRA_BINARIES_DIR" EXIT

# Create an extra command as an alias of an existing one.
create_extra_command() {
    local existing_command="$1"
    local script_path="$EXTRA_BINARIES_DIR/lix-$existing_command"

    cat > "$script_path" <<EOF
#!/bin/sh
# Re-adjust the existing command ARGV0 as we are not making Lix variants of those.
exec -a nix-$existing_command nix-$existing_command "\$@"
EOF
    chmod +x "$script_path"
}

# Example usage: create a test script
create_extra_command copy-closure
create_extra_command collect-garbage

export PATH="$PATH:/some/incorrect:/location/for/fun:$EXTRA_BINARIES_DIR"

# TODO: we do not support auto completion for external subcommands for now.
# Test the completion of an external subcommand
# [[ "$(NIX_GET_COMPLETIONS=1 nix copy-)" == $'normal\ncopy-closure\t' ]]
# [[ "$(NIX_GET_COMPLETIONS=1 nix collect)" == $'normal\ncollect-garbage\t' ]]

testSimpleExternalCommands() {
    # Test that an external subcommand without the experimental flag will fail
    # without mentioning the experimental feature.
    expect 1 nix copy-closure --help 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr

    # Test that an external subcommand can be run successfully.
    expect 0 lix --extra-experimental-features 'lix-custom-sub-commands' copy-closure --version 1>/dev/null
    expect 0 lix --extra-experimental-features 'lix-custom-sub-commands' collect-garbage --version 1>/dev/null

    # Test that the external subcommand can be run beyond `--help` processing successfully.
    expect 0 lix --extra-experimental-features 'lix-custom-sub-commands' collect-garbage --dry-run 1>/dev/null

    # Test that an external subcommand without the experimental flag will fail
    # without mentioning the experimental feature.
    expect 1 lix copy-closure --help 1>$TEST_ROOT/stdout 2>$TEST_ROOT/stderr
}

testSimpleExternalCommands
# Test that an external subcommand can be run successfully with a slightly modified PATH.
(
    # Split the PATH into its first component and the rest
    FIRST_PATH=${PATH%%:*}  # The first directory
    REST_PATH=${PATH#*:}    # The rest of the PATH

    echo "modified PATH: $PATH"
    # Rebuild PATH with the first directory moved to the second position
    export PATH=$(echo $REST_PATH | cut -d: -f1):$FIRST_PATH:$(echo $REST_PATH | cut -d: -s -f2-)

    testSimpleExternalCommands
)

# TODO: Test flags handling.

# TODO: Short, long and multiple flags should be tested as well.
# TODO: `--` special flag?
# TODO: test positional arguments, but only `nix-copy-closure` implements some and it's pesky to test here.
