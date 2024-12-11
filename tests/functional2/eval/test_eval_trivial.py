from functional2.testlib.fixtures import Nix

def test_trivial_addition(nix: Nix):
    assert nix.eval('1 + 1').json() == 2
