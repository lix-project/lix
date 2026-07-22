from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files({"simple": get_global_asset_pack("simple-drv")})
def test_path_info_all(nix: Nix):
    """Tests queryAllValidPaths"""

    nix.settings.add_xp_feature("nix-command")

    path = (
        nix.nix(["build", "--no-link", "--print-out-paths", "-f", "simple/simple.nix"])
        .run()
        .ok()
        .stdout_plain
    )

    paths = nix.nix(["path-info", "--all"]).run().ok().stdout_s.splitlines()
    assert len(paths) == 3
    assert path in paths
