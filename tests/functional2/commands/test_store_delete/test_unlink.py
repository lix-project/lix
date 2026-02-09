import sys

import pytest

from testlib.fixtures.file_helper import with_files, CopyFile
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset

_mo_files = {"config.nix": get_global_asset("config.nix"), "drv.nix": CopyFile("assets/drv.nix")}


@pytest.fixture(autouse=True)
def commands(nix: Nix):
    nix.settings.add_xp_feature("nix-command")


@pytest.mark.skipif(
    sys.platform == "darwin",
    reason="installables from symlinks to darwin-fake-redirected store paths are broken",
)
@with_files(_mo_files)
def test_store_delete_unlink(nix: Nix):
    cmd = nix.nix(["build", "-f", "drv.nix", "-o", "result", "--print-out-paths"])
    cwd = cmd.cwd

    built = cmd.run().ok().stdout_plain
    built_path = nix.physical_store_path_for(built)
    assert built_path.exists()

    result_path = cwd.joinpath("result")
    assert result_path.exists(follow_symlinks=False)
    assert result_path.is_symlink()

    # Deleting the "symlink-to-store-path" installable should fail, because the symlink itself
    # keeps the store path alive.
    nix.nix(["store", "delete", "./result"]).run().expect(1)
    assert result_path.exists(follow_symlinks=False)
    assert built_path.exists()

    # But with --unlink, it'll remove that root and thus actually delete the path.
    nix.nix(["store", "delete", "--unlink", "./result"]).run().ok()
    assert not result_path.exists(follow_symlinks=False), "--unlink didn't remove gc root"
    assert not built_path.exists(), "nix store delete didn't actually delete"


@pytest.mark.skipif(
    sys.platform == "darwin",
    reason="installables from symlinks to darwin-fake-redirected store paths are broken",
)
@with_files(_mo_files)
def test_store_delete_unlink_closure(nix: Nix):
    cmd = nix.nix(["build", "-f", "drv.nix", "-o", "result", "--print-out-paths"])
    cwd = cmd.cwd

    built = cmd.run().ok().stdout_plain
    result_path = cwd.joinpath("result")

    # We just built some paths; let's make sure they exist now, before we delete them.
    requisites = nix.nix(["path-info", "--recursive", built]).run().ok().stdout_plain.splitlines()
    req_paths = [nix.physical_store_path_for(req) for req in requisites]
    for req in req_paths:
        assert req.exists()

    nix.nix(["store", "delete", "--unlink", "--delete-closure", "./result"]).run().ok()
    assert not result_path.exists(follow_symlinks=False)

    for req in req_paths:
        assert not req.exists(), f"{req} should be deleted but it's still here"
