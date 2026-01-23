import pytest

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("dependencies"))
@pytest.mark.parametrize("algorithm", ["br", "zstd", "xz"])
def test_cache_compressions(nix: Nix, algorithm: str):
    nix.settings.add_xp_feature("nix-command")
    cache_uri = f"file://{nix.env.dirs.cache_dir}?compression={algorithm}"
    res = nix.nix_build(["dependencies.nix", "--no-out-link"]).run().ok()
    out_path = res.stdout_plain

    res = nix.nix(["copy", "--to", cache_uri, out_path]).run().ok()
    assert "copying 4 paths..." in res.stderr_plain
    hash1 = nix.hash_path(out_path)

    # clear the "cache cache"
    for f in (nix.env.dirs.home / ".cache" / "nix").glob("test-binary-cache*"):
        f.unlink()
    nix.clear_store()

    res = nix.nix(["copy", "--from", cache_uri, out_path, "--no-check-sigs"]).run().ok()
    assert "copying 4 paths..." in res.stderr_plain

    hash2 = nix.hash_path(out_path)

    assert hash1 == hash2
