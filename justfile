# https://just.systems/man/en/

clean:
    rm -rf build

setup:
    meson setup build --prefix="$PWD/outputs/out"

build *OPTIONS:
    meson compile -C build {{ OPTIONS }}

compile:
    just build

install *OPTIONS: (build OPTIONS)
    meson install -C build

test *OPTIONS:
    meson test -C build --print-errorlogs --quiet {{ OPTIONS }}
