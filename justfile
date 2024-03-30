# https://just.systems/man/en/

clean:
    rm -rf build

setup:
    meson setup build --prefix="$PWD/outputs/out"

build:
    meson compile -C build

compile:
    just build

install:
    meson install -C build

test *OPTIONS:
    meson test -C build --print-errorlogs --quiet {{ OPTIONS }}
