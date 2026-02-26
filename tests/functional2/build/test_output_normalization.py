from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files({"drv": get_global_asset_pack("simple-drv")})
def test_output_normalization(nix: Nix):
    result = nix.nix_build(["drv/simple.nix"]).run().ok().stdout_plain
    assert Path(result).stat().st_mtime == 1
