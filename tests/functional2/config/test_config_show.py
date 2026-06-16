from testlib.fixtures.nix import Nix
import pytest

pytestmark = pytest.mark.no_daemon


def test_config_env_var(nix: Nix):
    nix.settings.disabled = True
    nix.env["NIX_CONFIG"] = "cores = 4242\nexperimental-features = nix-command flakes"
    res = nix.nix(["config", "show"]).run().ok()
    assert "cores = 4242" in res.stdout_plain
    assert "experimental-features = flakes nix-command" in res.stdout_plain


def test_config_show_single_value(nix: Nix):
    res = nix.nix(["config", "show", "warn-dirty"], flake=True).run().ok()
    assert res.stdout_plain == "true"


def test_trusted_user_ignored(nix: Nix):
    """
    regression test for https://git.lix.systems/lix-project/lix/issues/1183
    """
    nix.settings.disabled = True
    nix.env["NIX_CONFIG"] = "experimental-features = nix-command\ntrusted-users = as-configured"
    res = nix.nix(["config", "show", "trusted-users"]).run().ok()
    assert res.stdout_plain == "as-configured"

    nix.env["NIX_CONFIG"] += "\nextra-trusted-users = extra"
    res = nix.nix(["config", "show", "trusted-users"]).run().ok()
    assert res.stdout_plain == "as-configured extra"
