# https://just.systems/man/en/
#
# Take a look at ./doc/manual/src/contributing/hacking.md for a detailed
# explanation on how to use this file!

# Pin the shell to bash (anything sufficiently POSIX-y would do)
# HACK: We use https://github.com/casey/just#positional-arguments
# and `"@$"` to forward arguments to the inner commands.
# The reason we require this is that `{{ OPTIONS }}` does not escape any values,
# and thus requires one additional level of escaping when running `just` commands with e.g. spaces in them.
# just provides no good solution to this problem, so we have to rely on its forwarding of arguments and shell semantics.
set shell := ["bash", "-uc"]

outdir := x"${out:-$PWD/outputs/out}"
builddir := "build"

# List all available targets
list:
    just --list

# Clean build artifacts and outputs.
clean:
    rm -rf {{ quote(builddir) }}/* {{ quote(builddir) }}/.* {{ quote(outdir) }}/* {{ quote(outdir) }}/.*
    cargo clean

# Prepare meson for building.
[positional-arguments]
setup *OPTIONS:
    meson setup {{ builddir }} --reconfigure --prefix="{{outdir}}" $mesonFlags "$@"

# Build lix with extra options
[positional-arguments]
build *OPTIONS:
    meson compile -C {{ builddir }} "$@"

alias compile := build

# `meson install` will automatically build anything that needs to be built to install it.
[doc("Install Lix for local development")]
[positional-arguments]
install *OPTIONS:
    meson install --quiet -C {{ builddir }} "$@"

# Run all tests tests (installs first).
[positional-arguments]
test *OPTIONS: (install)
    meson test -C {{ builddir }} --print-errorlogs --max-lines 10000 "$@"

# Run unit tests only
test-unit *OPTIONS: (test "--suite" "check")

# Run integration tests only
test-integration *OPTIONS: (test "--suite" "installcheck" OPTIONS)

# Run functional2 tests using pytest directly, allowing for additional arguments to be passed to pytest e.g. for more granular test selection
[positional-arguments]
test-functional2 *OPTIONS:
    cd tests/functional2 && python -m pytest -v "$@"

# special target for cargo because meson cannot be convinced to not mangle cargo test output,
# and getting properly colored test output any other way also doesn't look all that possible.
[positional-arguments]
test-rs *OPTIONS:
    meson test -C {{ builddir }} --interactive lix-rs-tests "$@"

# Lint with `clang-tidy`
lint:
    ninja -C build clang-tidy

alias clang-tidy-fix := lint-fix

# Fix lints with `clang-tidy-fix`
lint-fix:
    ninja -C build clang-tidy-fix
