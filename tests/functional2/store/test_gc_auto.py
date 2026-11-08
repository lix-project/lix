"""
Tests for automatic garbage collection (min-free/max-free options).

Auto-GC scans /proc for runtime roots, which is very slow under ASan, so these
tests wait for the collector instead of racing it with a short sleep.

There's a 15 minutes timeout on the test suite to detect hanging quickly.
"""

import shlex
from pathlib import Path

import pytest

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset

pytestmark = [pytest.mark.no_daemon, pytest.mark.timeout(900)]

_BASE_FILES = {"config.nix": get_global_asset("config.nix")}

_INITIAL_FREE_SPACE = 1100


def _write_free_space(path: Path, value: int) -> None:
    # avoid concurrent reads by auto-GC
    tmp = path.with_name(f"{path.name}.tmp")
    tmp.write_text(f"{value}\n")
    tmp.replace(path)


def _add_garbage(nix: Nix, files: Path) -> str:
    source = files / "garbage"
    source.write_text("garbage\n")
    return nix.nix_store(["--add", str(source)]).run().ok().stdout_plain


@with_files(_BASE_FILES)
def test_auto_gc_during_eval(nix: Nix, files: Path) -> None:
    free_space = files / "fake-free"
    nix.env.set_env("_NIX_TEST_FREE_SPACE_FILE", str(free_space))
    _write_free_space(free_space, _INITIAL_FREE_SPACE)

    garbage = _add_garbage(nix, files)
    garbage_path = nix.physical_store_path_for(garbage)
    assert garbage_path.exists()

    _write_free_space(free_space, 100)
    result = nix.eval('builtins.toFile "meow" "meow"', flags=["--min-free", "2000"])

    meow = nix.physical_store_path_for(result.json())
    assert meow.read_text() == "meow"
    assert not garbage_path.exists()


@with_files(_BASE_FILES)
def test_auto_gc_during_build(nix: Nix, files: Path) -> None:
    state = files / "gc-auto"
    state.mkdir()
    # To make matters simple, the derivation observes the real store itself, so
    # we cannot run this in the sandbox.
    nix.settings.sandbox = False
    # The builder may run as a different user.
    state.chmod(0o777)

    free_space = state / "fake-free"
    nix.env.set_env("_NIX_TEST_FREE_SPACE_FILE", str(free_space))
    _write_free_space(free_space, _INITIAL_FREE_SPACE)

    garbage = _add_garbage(nix, files)
    garbage_path = nix.physical_store_path_for(garbage)
    assert garbage_path.exists()

    # NOTE: it is tempting to pass `garbage` as a store path via the Nix
    # language in the code, but doing so will root it. Defeating what we intend
    # to do here.
    garbage_file = state / "garbage"
    garbage_file.write_text(f"{garbage}\n")

    free_space_arg = shlex.quote(str(free_space))
    garbage_file_arg = shlex.quote(str(garbage_file))
    expr = f"""
        with import ./config.nix; mkDerivation {{
          name = "gc-auto";
          buildCommand = ''
            set -eu
            mkdir "$out"
            echo foo > "$out/bar"

            garbage=$(cat {garbage_file_arg})
            [ -e "$garbage" ] || {{ echo "missing store path: $garbage" >&2; exit 1; }}

            # Pretend to run out of space and wait for auto-GC to free the path.
            echo 100 > {free_space_arg}.tmp
            mv {free_space_arg}.tmp {free_space_arg}
            while [ -e "$garbage" ]; do
                sleep 1
            done
          '';
        }}
    """

    result = files / "result"
    nix.nix_build(
        ["-E", expr, "-o", str(result), "--min-free", "1000", "--max-free", "1000000"], cwd=files
    ).run().ok()

    assert (result / "bar").read_text() == "foo\n"
    assert not garbage_path.exists()
