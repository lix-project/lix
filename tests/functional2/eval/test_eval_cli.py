from pathlib import Path
from typing import Any

import pytest

from functional2.testlib.fixtures.file_helper import with_files, CopyFile, Symlink, File
from functional2.testlib.fixtures.nix import Nix


@pytest.fixture(autouse=True)
def nix_command_feature(nix: Nix):
    nix.settings.feature("nix-command")


@pytest.mark.parametrize(
    ("stdin", "exp", "flags"),
    [
        (
            b"""
                {
                    bar = 3 + 1;
                    foo = 2 + 2;
                }
            """,
            "{ bar = 4; foo = 4; }",
            ["-f", "-"],
        )
    ],
)
@with_files({"eval.nix": CopyFile("assets/eval.nix")})
def test_valid_eval_stdin(nix: Nix, stdin: bytes, exp: Any, flags: list[str]):
    cmd = nix.nix(["eval", *flags]).with_stdin(stdin)
    res = cmd.run().ok()
    assert res.stdout_plain == exp


@with_files({"eval.nix": CopyFile("assets/eval.nix")})
def test_valid_filename_stdin(nix: Nix, files: Path):
    stdin = (files / "eval.nix").read_bytes()
    res = nix.nix(["eval", "int", "-f", "-"]).with_stdin(stdin).run().ok()
    assert res.stdout_plain == "123"

    res = nix.nix_instantiate(["-A", "int", "--eval", "-"]).with_stdin(stdin).run().ok()
    assert res.stdout_plain == "123"


@pytest.mark.parametrize(
    ("flags", "exp"),
    [
        (["int"], "123"),
        (["str"], '"foo\\nbar"'),
        (["str", "--raw"], "foo\nbar"),
        (["attr"], '{ foo = "bar"; }'),
        (["attr", "--json"], '{"foo":"bar"}'),
    ],
)
@with_files({"eval.nix": CopyFile("assets/eval.nix")})
def test_valid_eval_f(nix: Nix, flags: list[str], exp: str):
    # nix3 cli
    res = nix.nix(["eval", *flags, "-f", "./eval.nix"]).run().ok()
    assert res.stdout_plain == exp

    # legacy cli
    res = nix.nix_instantiate(["-A", *flags, "--eval", "./eval.nix"]).run().ok()
    assert res.stdout_plain == exp


@pytest.mark.parametrize(
    ("expr", "exp"),
    [("assert 1 + 2 == 3; true", "true"), ('{"assert"=1;bar=2;}', '{ "assert" = 1; bar = 2; }')],
)
@pytest.mark.parametrize("long", [False, True])
def test_valid_expr(nix: Nix, expr: str, exp: str, long: bool):
    flag = "--expr" if long else "-E"
    res = nix.nix(["eval", flag, expr]).run().ok()
    assert res.stdout_plain == exp

    res = nix.nix_instantiate(["--eval", flag, expr]).run().ok()
    assert res.stdout_plain == exp


@with_files({"eval.nix": CopyFile("assets/eval.nix")})
def test_invalid_no_coercible_values(nix: Nix):
    """Non-coercible values throws errors under `--raw`"""
    res = nix.nix(["eval", "int", "--raw", "-f", "./eval.nix"]).run().expect(1)
    assert "error: cannot coerce an integer to a string: 123" in res.stderr_plain

    res = nix.nix_instantiate(["-A", "int", "--raw", "./eval.nix"]).run().expect(1)
    assert (
        "error: expression was expected to be a derivation or collection of derivations, but instead was an integer"
        in res.stderr_plain
    )


def test_invalid_top_level_error(nix: Nix):
    """Top-level eval errors should be printed to stderr with a traceback."""
    res = nix.nix(["eval", "--expr", 'throw "a sample throw message"']).run().expect(1)
    assert "a sample throw message" in res.stderr_plain
    assert "caused by explicit throw" in res.stderr_plain


def test_valid_nested_error(nix: Nix):
    """But errors inside something should print an elided version, and exit with 0."""
    res = nix.nix(["eval", "--expr", '{ throws = throw "a sample throw message"; }']).run().ok()
    assert res.stdout_plain == "{ throws = «error: a sample throw message»; }"


def test_valid_restricted_toFile(nix: Nix):  # noqa: N802 # builtin name
    """Check if toFile can be utilized during restricted eval"""
    res = (
        nix.nix(["eval", "--restrict-eval", "--expr", 'import (builtins.toFile "source" "42")'])
        .run()
        .ok()
    )
    assert res.stdout_plain == "42"


@with_files({"cycle.nix": Symlink("cycle.nix")})
# timeout given in seconds
@pytest.mark.timeout(30)
def test_invalid_no_hang_symlink_cycle(nix: Nix):
    """Check that symlink cycles don't cause a hang."""
    res = nix.nix(["eval", "--file", "cycle.nix"]).run().expect(1)
    assert (
        "error: too many symbolic links encountered while traversing the path" in res.stderr_plain
    )


@with_files({"xyzzy": {"default.nix": File("123")}, "foo": {"bar": Symlink("../xyzzy")}})
def test_valid_symlink_resolution(nix: Nix):
    """Check that relative symlinks are resolved correctly."""
    res = nix.nix(["eval", "--impure", "--expr", "import ./foo/bar"]).run().ok()
    assert res.stdout_plain == "123"


def test_valid_warn_unknown_setting(nix: Nix):
    """Test that unknown settings are warned about"""
    res = (
        nix.nix(["eval", "--option", "foobar", "baz", "--expr", '"foxes are cute"', "--raw"])
        .run()
        .ok()
    )
    assert res.stdout_plain == "foxes are cute"
    assert "warning: unknown setting 'foobar'" in res.stderr_plain
