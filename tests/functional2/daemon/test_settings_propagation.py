import pytest

from pathlib import Path
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.nix import Nix, NixDaemon
from testlib.fixtures.file_helper import File, with_files


@pytest.mark.parametrize("daemon", ["legacy-combined"], indirect=True)
def test_xp_features_from_daemon_argv(nix: Nix, daemon: NixDaemon):
    nix.settings.add_xp_feature("nix-command")
    assert nix.env.dirs.nix_state_dir is not None
    sockets_dir = nix.env.dirs.nix_state_dir / "daemon-socket"

    # Start as legacy-combined so that the fixture does not inject rpc-sockets
    # into the daemon's config. We will now transform this into a lix-xp-1
    # thing.
    with daemon(
        nix, args=["--extra-experimental-features", "rpc-sockets"], protocol="legacy-combined"
    ) as inner:
        inner.settings.store = f"unix://{sockets_dir}?protocol=lix-xp-1"
        inner.nix(["store", "ping"]).run().ok()


@with_files({"hello": File("hello\n")})
@pytest.mark.parametrize("daemon", ["legacy-combined"], indirect=True)
def test_store_from_daemon_argv(nix: Nix, daemon: NixDaemon, env: ManagedEnv):
    nix.settings.add_xp_feature("nix-command")
    alt_root = env.dirs.test_root / "alt"
    (alt_root / "nix/store").mkdir(parents=True)
    (alt_root / "nix/var/nix").mkdir(parents=True)

    alt_store = f"local?root={alt_root}&log={alt_root}/var/log/nix&state={alt_root}/nix/var/nix"

    with daemon(nix, args=["--store", alt_store]) as inner:
        added = Path(inner.nix(["store", "add-path", "hello"]).run().ok().stdout_plain.strip())

        # the harness relocates NIX_STORE_DIR, so `added` is already rooted at the
        # test store dir; only the basename is comparable across the two stores.
        name = added.name
        assert (alt_root / "nix/store" / name).exists()
        assert not (nix.store_dir / name).exists()
