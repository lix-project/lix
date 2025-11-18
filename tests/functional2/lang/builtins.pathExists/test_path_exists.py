from pathlib import Path
from collections.abc import Callable

from functional2.lang.test_lang import test_eval as nix_eval
from functional2.testlib.fixtures.file_helper import (
    with_files,
    CopyFile,
    AssetSymlink,
    File,
    Symlink,
)
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@with_files(
    {
        "in.nix": CopyFile("in.nix"),
        "out.exp": AssetSymlink("eval-okay.out.exp"),
        "err.exp": AssetSymlink("eval-okay.err.exp"),
        "symlink-resolution": {
            "foo": {"lib": {"default.nix": File('"test"')}, "overlays": Symlink("../overlays")},
            "overlays": {"overlay.nix": File("import ../lib")},
            "broken": Symlink("nonexistent"),
        },
        "lib.nix": CopyFile("../lib.nix"),
    }
)
def test_path_exists(nix: Nix, files: Path, snapshot: Callable[[str], Snapshot]):
    nix_eval(files, nix, [], snapshot)
