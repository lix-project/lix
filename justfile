# https://just.systems/man/en/

# List all available targets
list:
    just --list

# Clean build artifacts
clean:
    rm -rf build

# Prepare meson for building
setup *OPTIONS:
    meson setup build --prefix="$PWD/outputs/out" $mesonFlags {{ OPTIONS }}

# Build lix
build *OPTIONS:
    meson compile -C build {{ OPTIONS }}

alias compile := build

# Install lix for local development
install *OPTIONS: (build OPTIONS)
    meson install -C build

# Run tests
test *OPTIONS:
    meson test -C build --print-errorlogs {{ OPTIONS }}

alias clang-tidy := lint

lint:
    ninja -C build clang-tidy

alias clang-tidy-fix := lint-fix

lint-fix:
    ninja -C build clang-tidy-fix
