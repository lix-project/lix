from collections.abc import Callable
from pathlib import Path

from functional2.testlib.fixtures.file_helper import with_files, File, AssetSymlink, Symlink
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@with_files(
    {
        "in.nix": File("import symlink-resolution/foo/overlays/overlay.nix"),
        "out.exp": AssetSymlink("assets/symlink-resolution.out.exp"),
        "symlink-resolution": {
            "foo": {"lib": {"default.nix": File('"test"')}, "overlays": Symlink("../overlays")},
            "overlays": {"overlay.nix": File("import ../lib")},
        },
    }
)
def test_symlink_resolution(nix: Nix, files: Path, snapshot: Callable[[str], Snapshot]):
    res = nix.nix_instantiate(["--eval", "--strict", files / "in.nix"]).run().ok()
    assert snapshot("out.exp") == res.stdout_plain
    assert not res.stderr_plain
