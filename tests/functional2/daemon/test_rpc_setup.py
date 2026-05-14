import pytest

from testlib.fixtures.nix import Nix, NixDaemon


def test_legacy_sockets_always_appear(nix: Nix, daemon: NixDaemon):
    sockets_dir = nix.env.dirs.nix_state_dir / "daemon-socket"
    with daemon(nix):
        assert sockets_dir.exists()
        assert sockets_dir.is_dir()
        assert (sockets_dir / "socket").exists()
        assert (sockets_dir / "socket").is_socket()


@pytest.mark.no_daemon  # We do the daemon config ourselves here
@pytest.mark.parametrize("daemon", ["legacy"], indirect=True)
def test_xp_sockets_dont_always_appear(nix: Nix, daemon: NixDaemon):
    sockets_dir = nix.env.dirs.nix_state_dir / "daemon-socket"
    with daemon(nix):
        assert list(sockets_dir.glob("./**")) == [sockets_dir, sockets_dir / "socket"]
