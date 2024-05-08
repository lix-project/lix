# https://just.systems/man/en/

# List all available targets
list:
    just --list

# Clean build artifacts
clean:
    rm -rf build

# Prepare meson for building
setup:
    meson setup build --prefix="$PWD/outputs/out"

# Build lix
build *OPTIONS:
    meson compile -C build {{ OPTIONS }}

alias compile := build

# Install lix for local development
install *OPTIONS: (build OPTIONS)
    meson install -C build

# Run tests
test *OPTIONS:
    meson test -C build --print-errorlogs --quiet {{ OPTIONS }}
