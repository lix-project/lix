from functional2.testlib.fixtures.nix import Nix
import pytest
from typing import NamedTuple
from textwrap import dedent


class ShouldError(NamedTuple):
    expr: str
    attr: str
    error: str


ERR_CASES: list[ShouldError] = [
    # FIXME(jade): expect-test system for pytest that allows for updating these easily
    ShouldError("{}", '"x', """error: missing closing quote in selection path '"x'"""),
    ShouldError(
        "[]",
        "x",
        """error: the value being indexed in the selection path 'x' at '' should be a set but is a list: [ ]""",
    ),
    ShouldError(
        "{}",
        "1",
        """error: the expression selected by the selection path '1' should be a list but is a set: { }""",
    ),
    ShouldError("{}", ".", """error: empty attribute name in selection path '.'"""),
    ShouldError(
        '{ x."" = 2; }', 'x.""', """error: empty attribute name in selection path 'x.""'"""
    ),
    ShouldError(
        '{ x."".y = 2; }', 'x."".y', """error: empty attribute name in selection path 'x."".y'"""
    ),
    ShouldError(
        "[]", "1", """error: list index 1 in selection path '1' is out of range for list [ ]"""
    ),
    ShouldError(
        "{ x.y = { z = 2; a = 3; }; }",
        "x.y.c",
        dedent("""\
            error: attribute 'c' in selection path 'x.y.c' not found inside path 'x.y', whose contents are: { a = 3; z = 2; }
                   Did you mean one of a or z?"""),
    ),
]


# I do not know why it does this, but I guess it makes sense as allowing a tool
# to pass -A unconditionally and then allow a blank attribute to mean the whole
# thing
def test_attrpath_accepts_empty_attr_as_no_attr(nix: Nix):
    assert (
        nix.nix_instantiate(["--eval", "--expr", "{}", "-A", ""]).run().ok().stdout_plain == "{ }"
    )


@pytest.mark.parametrize(("expr", "attr", "error"), ERR_CASES)
def test_attrpath_error(nix: Nix, expr: str, attr: str, error: str):
    res = nix.nix_instantiate(["--eval", "--expr", expr, "-A", attr]).run()

    assert res.expect(1).stderr_plain == error
