import re
from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("dependencies"))
def test_compression_levels(nix: Nix):
    cache_dir1 = nix.env.dirs.cache_dir / "test1"
    cache_dir2 = nix.env.dirs.cache_dir / "test2"

    nix.settings.feature("nix-command")
    res = nix.nix_build(["dependencies.nix", "--no-out-link"]).run().ok()
    out_path = res.stdout_plain

    def get_size_for_level(level: int, c_dir: Path) -> int:
        cpy_res = (
            nix.nix(
                [
                    "copy",
                    "--to",
                    f"file://{c_dir}?compression=xz&compression-level={level}",
                    out_path,
                ],
                build=True,
            )
            .run()
            .ok()
        )
        assert "copying 4 paths..." in cpy_res.stderr_plain

        size = 0
        for f in c_dir.glob("*.narinfo"):
            size += int(re.search(r"FileSize: (\d+)\n", f.read_text()).group(1))

        return size

    size_0 = get_size_for_level(0, cache_dir1)

    size_5 = get_size_for_level(5, cache_dir2)

    assert size_0 > size_5
