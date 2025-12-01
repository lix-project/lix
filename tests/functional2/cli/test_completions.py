from pathlib import Path
from textwrap import dedent

import pytest

from functional2.testlib.fixtures.file_helper import File, with_files, EnvTemplate
from functional2.testlib.fixtures.nix import Nix

files = {
    "dep": {"flake.nix": File("{outputs = i: { };}")},
    "foo": {
        "flake.nix": EnvTemplate(
            dedent("""
            {
                inputs.a.url = "path:@HOME@/dep";

                outputs = i: {
                    sampleOutput = 1;
                };
            }
        """)
        )
    },
    "bar": {
        "flake.nix": EnvTemplate(
            dedent("""
            {
                inputs.b.url = "path:@HOME@/dep";

                outputs = i: {
                    sampleOutput = 1;
                };
            }
        """)
        )
    },
    "err": {"flake.nix": File('throw "error"')},
}


@pytest.fixture(autouse=True)
def nix_command_feature(nix: Nix):
    # "flakes" seems to be the feature actually responsible for
    # resolving the completions correctly
    #
    # Commentator2.0: no clue why...
    nix.settings.feature("nix-command", "flakes")


def test_completions_valid(nix: Nix):
    nix.env.set_env("NIX_GET_COMPLETIONS", "1")
    res = nix.nix(["buil"]).run().ok()
    assert res.stdout_s == "normal\nbuild\t\n"

    nix.env.set_env("NIX_GET_COMPLETIONS", "2")
    res = nix.nix(["flake", "metad"]).run().ok()
    assert res.stdout_s == "normal\nmetadata\t\n"


def test_completions_invalid_envar(nix: Nix):
    nix.env.set_env("NIX_GET_COMPLETIONS", "-")
    res = nix.nix([]).run().expect(1)
    assert (
        "error: Invalid value for environment variable NIX_GET_COMPLETIONS: -\n" in res.stderr_plain
    )


@pytest.mark.parametrize("position", [0, 4])
def test_completions_invalid_position(nix: Nix, position: int):
    nix.env.set_env("NIX_GET_COMPLETIONS", str(position))
    res = nix.nix(["build", "a"]).run().expect(1)
    assert f"error: Invalid word number to get completion for: {position}" in res.stderr_plain


@with_files({"foo": {}})
def test_completions_existing_filename(nix: Nix):
    nix.env.set_env("NIX_GET_COMPLETIONS", "2")
    res = nix.nix(["build", "./f"]).run().ok()
    assert res.stdout_s == "filenames\n./foo\t\n"


def test_completions_nonexisting_filename(nix: Nix):
    nix.env.set_env("NIX_GET_COMPLETIONS", "2")
    res = nix.nix(["build", "./nonexistent"]).run().ok()
    assert res.stdout_s == "filenames\n"


@pytest.mark.parametrize(
    ("args", "pos"),
    [
        (["build", "./foo", "--override-input", ""], 4),
        (["flake", "show", "./foo", "--override-input", ""], 5),
        (["flake", "update", "--flake", "./foo", ""], 5),
        # tilde expansion
        (["build", "~/foo", "--override-input", ""], 4),
        (["flake", "update", "--flake", "~/foo", ""], 5),
        # Out of order
        (["build", "--override-input", "", "", "./foo"], 3),
    ],
)
@with_files(files)
def test_completions_input_override(nix: Nix, args: list[str], pos: int):
    nix.env.set_env("NIX_GET_COMPLETIONS", str(pos))
    res = nix.nix(args).run().ok()
    assert res.stdout_s == "normal\na\t\n"


@pytest.mark.parametrize(
    ("args", "pos"),
    [
        (["build", "./foo", "./bar", "--override-input", ""], 5),
        (["build", "./foo", "--override-input", "", "", "./bar"], 4),
    ],
)
@with_files(files)
def test_completions_multiple_input_flakes(nix: Nix, args: list[str], pos: int):
    nix.env.set_env("NIX_GET_COMPLETIONS", f"{pos}")
    res = nix.nix(args).run().ok()
    assert res.stdout_s == "normal\na\t\nb\t\n"


@with_files(files)
def test_completions_flake_update(nix: Nix, files: Path):
    cwd = files / "foo"
    nix.env.set_env("NIX_GET_COMPLETIONS", "3")
    cmd = nix.nix(["flake", "update", ""])
    cmd.cwd = cwd
    res = cmd.run().ok()
    assert res.stdout_s == "normal\na\t\n"


def test_flag_completion(nix: Nix):
    nix.env["NIX_GET_COMPLETIONS"] = "2"
    res = nix.nix(["build", "--dry"]).run().ok()
    assert "--dry-run" in res.stdout_plain
    assert "Show what this command would do without doing it" in res.stdout_plain


def test_option_completion(nix: Nix):
    nix.env["NIX_GET_COMPLETIONS"] = "3"
    res = nix.nix(["build", "--option", "allow-import-"]).run().ok()
    assert "allow-import-from-derivation" in res.stdout_plain


@pytest.mark.skip("SKIP(Hufschmitt 2022-07): Options as CLI-flags currently don't have completions")
def test_option_cli_flag_completion(nix: Nix):
    nix.env["NIX_GET_COMPLETIONS"] = "2"
    res = nix.nix(["build", "--allow-import-from"]).run().ok()
    assert "allow-import-from-derivation" in res.stdout_plain


@pytest.mark.parametrize(
    ("args", "pos", "exp"),
    [
        (["./foo#sam"], 2, "attrs\n./foo#sampleOutput\t\n"),
        (["./err#"], 2, "attrs\n"),
        (["--file", "./foo/flake.nix", "outp"], 4, "attrs\noutputs\t\n"),
        (["--file", "./err/flake.nix", "outp"], 4, "attrs\n"),
    ],
)
@with_files(files)
def test_valid_attr_path_completion(nix: Nix, args: list[str], pos: int, exp: str):
    nix.env["NIX_GET_COMPLETIONS"] = f"{pos}"
    res = nix.nix(["eval", *args]).run().ok()
    assert res.stdout_s == exp
