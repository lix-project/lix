from testlib.fixtures.nix import Nix


# ruff: noqa: N802
def test_fetchTree_presence(nix: Nix):
    """Ensures that fetchTree is actually absent if flakes are disabled"""
    nix.settings.add_xp_feature("nix-command")
    assert nix.eval("builtins ? fetchTree", nix.settings).json() is False

    nix.settings.add_xp_feature("flakes")
    assert nix.eval("builtins ? fetchTree", nix.settings).json() is True
