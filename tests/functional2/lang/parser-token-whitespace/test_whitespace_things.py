from pathlib import Path
from collections.abc import Callable

import pytest

from testlib.fixtures.nix import Nix
from testlib.fixtures.snapshot import Snapshot
from testlib.utils import functional2_base_folder


@pytest.mark.parametrize(
    ("expr", "exit_code", "exit_code_depr"),
    [
        ("00012.3", 1, 0),
        ("0a", 1, 0),
        ("0https://a", 1, 0),
        ("0.0.0", 1, 0),
        ("""foo"1"2""", 1, 0),
        ("0x10", 1, 0),
        ("0.", 1, 1),
        ("1.", 0, 0),
        ("0.a", 1, 0),
        ("1.a", 1, 0),
        (""" 0."" """, 1, 0),
        (""" 1."" """, 1, 0),
        # test against false positives
        ("(0)(0)", 0, 0),
        ('a("")', 0, 0),
        ("(a).a", 0, 0),
        # not deprecated atm but probably should be in the future (FIXME piegames; 2026-01-30)
        ("(a).0", 0, 0),
        # unrelated syntax errors which don't trigger the deprecation
        ("00.", 1, 1),
    ],
)
def test_whitespace(
    nix: Nix,
    expr: str,
    exit_code: int,
    exit_code_depr: int,
    snapshot: Callable[[str], Snapshot],
    files: Path,
):
    expr = expr.strip()
    for i, expr in enumerate((f"({expr})", f"[{expr}]")):
        f_expr = expr.replace("/", "-")
        for f in [
            f"{f_expr}.out.exp",
            f"{f_expr}.err.exp",
            f"{f_expr}-depr.out.exp",
            f"{f_expr}-depr.err.exp",
        ]:
            (files / f).symlink_to(functional2_base_folder / "lang" / "parser-token-whitespace" / f)

        full_expr = f"with {{}}; {expr}"
        res = (
            nix.nix_instantiate(
                ["--parse", "--extra-deprecated-features", "url-literals", "-E", full_expr]
            )
            .run()
            .expect(exit_code)
        )
        assert snapshot(f"{f_expr}.out.exp") == res.stdout_s
        assert snapshot(f"{f_expr}.err.exp") == res.stderr_s

        res = (
            nix.nix_instantiate(
                [
                    "--parse",
                    "--extra-deprecated-features",
                    "tokens-no-whitespace url-literals",
                    "-E",
                    full_expr,
                ]
            )
            .run()
            .expect(exit_code_depr)
        )
        assert snapshot(f"{f_expr}-depr.out.exp") == res.stdout_s
        assert snapshot(f"{f_expr}-depr.err.exp") == res.stderr_s
