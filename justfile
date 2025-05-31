# https://just.systems/man/en/

# List all available targets
list:
    just --list

# Clean build artifacts
clean:
    rm -rf build

# Prepare meson for building with extra options
setup-custom *OPTIONS:
    meson setup build --prefix="$PWD/outputs/out" $mesonFlags {{ OPTIONS }}

# Prepare meson for building
setup: (setup-custom)

# Build lix with extra options
build-custom *OPTIONS:
    meson compile -C build {{ OPTIONS }}

# Build lix
build: (build-custom)

alias compile := build

# Install lix for local development with extra options
install-custom *OPTIONS: (build-custom OPTIONS)
    meson install -C build

# Install lix for local development
install: (install-custom)

# Run tests (usually requires `install`) with extra options
test *OPTIONS:
    meson test -C build --print-errorlogs {{ OPTIONS }}

# Run unit tests only
test-unit *OPTIONS: (test "--suite" "check")

# Run integration tests only
test-integration *OPTIONS: install (test "--suite" "installcheck")

# Run functional2 tests using pytest directly, allowing for additional arguments to be passed to pytest e.g. for more granular test selection
test-functional2 *OPTIONS:
    cd tests && python -m pytest -v {{ OPTIONS }} functional2

alias clang-tidy := lint

# Lint with `clang-tidy`
lint:
    ninja -C build clang-tidy

alias clang-tidy-fix := lint-fix

# Fix lints with `clang-tidy-fix`
lint-fix:
    ninja -C build clang-tidy-fix
