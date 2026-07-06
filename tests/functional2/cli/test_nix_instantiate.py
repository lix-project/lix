from testlib.fixtures.nix import Nix
import pytest


pytestmark = pytest.mark.no_daemon


"""
Unknown setting warning
NOTE(cole-h): behavior is different depending on the order, which is why we test an unknown option
before and after the `'{}'`!
"""


def test_unknown_option_first(nix: Nix):
    res = nix.nix_instantiate(["--option", "foobar", "baz", "--expr", "{}"]).run().ok()
    assert "warning: unknown setting 'foobar'" in res.stderr_plain


def test_unknown_option_later(nix: Nix):
    res = nix.nix_instantiate(["--expr", "{}", "--option", "foobar", "baz"]).run().ok()
    assert "warning: unknown setting 'foobar'" in res.stderr_plain
