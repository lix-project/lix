import pytest
from testlib.fixtures.nix import Nix, NixDaemon


@pytest.fixture(autouse=True)
def setup(nix: Nix):
    nix.settings.add_xp_feature("nix-command")


def test_ping(nix: Nix):
    info = nix.nix(["store", "ping"]).run().ok().stderr_plain
    assert "Store URL: local" in info


def test_ping_json(nix: Nix):
    info = nix.nix(["store", "ping", "--json"]).run().ok().json()
    assert info["url"] == "local"


def test_ping_daemon(nix: Nix, daemon: NixDaemon):
    version = nix.nix(["daemon", "--version"]).run().ok().stdout_plain
    version = version.splitlines()[0].split()[-1]
    with daemon(nix) as inner:
        info = nix.nix(["store", "ping"]).run().ok().stderr_plain
        assert f"Version: {version}" in info
        info = inner.nix(["store", "ping", "--json"]).run().ok().json()
        assert info["url"] == inner.settings.store
        assert info["version"] == version


def test_ping_bad_store(nix: Nix):
    nix.settings.store = f"unix:{nix.env.dirs.home}"
    error = nix.nix(["store", "ping"]).run().expect(1).stderr_plain
    assert "could not connect" in error
