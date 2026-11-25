from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from testlib.fixtures.nix import Nix
import pytest


@with_files(get_global_asset_pack("dependencies"))
@pytest.mark.parametrize("args", [[], ["--builders", "auto - - 8 1", "-j0"]])
def test_build_into_cache(nix: Nix, args: list[str]):
    """building into caches should fail with a descriptive error message"""
    res = (
        nix.nix_build(["--store", f"file://{nix.env.dirs.cache_dir}", *args, "dependencies.nix"])
        .run()
        .expect(1)
    )

    assert "building, substituting and repairing not supported" in res.stderr_plain
