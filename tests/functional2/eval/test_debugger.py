import re
from textwrap import dedent

from testlib.fixtures.nix import Nix


def test_regression_9932(nix: Nix):
    cmd = nix.nix(["eval", "--debugger", "--expr", '(_: throw "oh snap") 42'], flake=True)
    cmd.with_stdin(b":env")
    res = cmd.run().expect(1)
    assert "error: oh snap" in res.stderr_plain


def test_debugger_output(nix: Nix):
    expr = dedent("""
        let x.a = 1; in
        with x;
        (_: builtins.seq x.a (throw "oh snap")) x.a
    """)

    res = (
        nix.nix(["eval", "--debugger", "--expr", expr], flake=True)
        .with_stdin(b":env\n")
        .run()
        .expect(1)
    )
    assert "error: oh snap" in res.stderr_plain
    assert re.findall(r"with: .*a", res.stdout_plain)
    assert re.findall(r"static: .*x", res.stdout_plain)
