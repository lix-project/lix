from pathlib import Path
from collections.abc import Callable

from lang.test_lang import test_eval_okay as nix_eval
from testlib.fixtures.file_helper import with_files, CopyFile, AssetSymlink, File, Symlink
from testlib.fixtures.nix import Nix
from testlib.fixtures.snapshot import Snapshot


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
