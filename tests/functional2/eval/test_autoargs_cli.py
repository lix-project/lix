from typing import Any

import pytest

from testlib.fixtures.nix import Nix


def do_evaluate(nix: Nix, args: list[str], expect_success: bool = True) -> dict[str, Any] | str:
    res = (
        nix.nix_instantiate(
            [
                "--eval",
                "--json",
                "-E",
                "{ arg1, arg2 ? null }: { inherit arg1 arg2; }",
                "--strict",
                *args,
            ]
        )
        .run()
        .expect(0 if expect_success else 1)
    )

    if expect_success:
        return res.json()
    return res.stderr_s


def test_trivial(nix: Nix):
    res = do_evaluate(nix, args=["--arg", "arg1", "[ 1 2 3 ]", "--arg", "arg2", "1"])

    assert res["arg1"] == [1, 2, 3]
    assert res["arg2"] == 1


def test_recursive(nix: Nix):
    res = do_evaluate(
        nix,
        args=["--arg", "arg1.foo.bar", "1", "--arg", "arg1.foo.baz", "2", "--arg", "arg1.bar", "3"],
    )

    assert res["arg1"] == {"foo": {"bar": 1, "baz": 2}, "bar": 3}
    assert res["arg2"] is None


@pytest.mark.parametrize(
    ("attribute_path", "expected"),
    [("arg1", 2), ("arg1.foo", {"foo": 2}), ("arg1.foo.bar", {"foo": {"bar": 2}})],
)
def test_override(nix: Nix, attribute_path: str, expected: Any):
    res = do_evaluate(nix, args=["--arg", attribute_path, "1", "--arg", attribute_path, "2"])

    assert res["arg1"] == expected


def test_quoting(nix: Nix):
    res = do_evaluate(
        nix,
        args=[
            "--arg",
            "arg1.foo.bar",
            "1",
            "--arg",
            'arg1."foo bar baz".baz',
            "2",
            "--arg",
            "arg1.bar",
            "2",
        ],
    )

    assert res["arg1"] == {"foo": {"bar": 1}, "bar": 2, "foo bar baz": {"baz": 2}}
    assert res["arg2"] is None


def test_quoting_error(nix: Nix):
    res = do_evaluate(nix, ["--arg", 'arg1."foo bar.baz', "1"], expect_success=False)
    assert "error: missing closing quote in selection path 'arg1.\"foo bar.baz'" in res


def test_trailing_dot(nix: Nix):
    # This is what `parseAttrPath` from `libutil` does and is consistent with the selection path
    # passed to e.g. `nix-build -A`.
    res = do_evaluate(nix, args=["--arg", "arg1.bar.", "[ 1 2 3 ]"], expect_success=False)

    assert (
        "error: Trailing dot on the right-hand side of path expr 'arg1.bar.' is not allowed!" in res
    )


@pytest.mark.parametrize(
    "args",
    [
        ["--arg", "arg1.foo", "1", "--arg", "arg1.foo.bar", "2"],
        ["--arg", "arg1.foo.bar", "2", "--arg", "arg1.foo", "1"],
    ],
)
def test_conflict(nix: Nix, args: list[str]):
    res = do_evaluate(nix, args, expect_success=False)

    assert (
        "error: Cannot set arg1.foo.bar via --arg/--argstr when it's the path-extension of another auto-argument!"
        in res
    )


@pytest.mark.parametrize("selection", ["foo..bar", "foo.bar.."])
def test_no_empty_items(nix: Nix, selection: str):
    res = do_evaluate(nix, ["--arg", selection, "1"], expect_success=False)
    assert f"error: consecutive dots not allowed in selection path '{selection}'" in res
