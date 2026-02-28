from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files({"simple": get_global_asset_pack("simple-drv")})
def test_path_from_hash_part(nix: Nix):
    nix.settings.add_xp_feature("nix-command")

    path = (
        nix.nix(["build", "--no-link", "--print-out-paths", "-f", "simple/simple.nix"])
        .run()
        .ok()
        .stdout_plain
    )

    hash_part = Path(path).name[0:32]

    path2 = nix.nix(["store", "path-from-hash-part", hash_part]).run().ok().stdout_plain

    assert path == path2
