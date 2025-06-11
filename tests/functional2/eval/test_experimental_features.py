from functional2.testlib.fixtures.nix import Nix


# ruff: noqa: N802
def test_fetchTree_presence(nix: Nix):
    """Ensures that fetchTree is actually absent if flakes are disabled"""
    settings = nix.settings.feature("nix-command")
    assert nix.eval("builtins ? fetchTree", settings).json() is False

    settings.feature("flakes")
    assert nix.eval("builtins ? fetchTree", settings).json() is True
