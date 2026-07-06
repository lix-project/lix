from testlib.fixtures.nix import Nix
import pytest


pytestmark = pytest.mark.no_daemon


def test_no_op(nix: Nix):
    res = nix.nix_env(["--foo"]).run().expect(1)
    assert "no operation" in res.stderr_plain


def test_unknown_flag(nix: Nix):
    res = nix.nix_env(["-q", "--foo"]).run().expect(1)
    assert "unknown flag" in res.stderr_plain
