from functional2.testlib.fixtures.nix import Nix


def test_trivial_addition(nix: Nix):
    assert nix.eval("1 + 1").json() == 2


def test_trivial_assert(nix: Nix):
    assert nix.eval("assert 1 + 2 == 3; true").json() is True
