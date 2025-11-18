from collections.abc import Callable
from pathlib import Path

from functional2.lang.test_lang import test_eval as nix_eval
from functional2.testlib.fixtures.file_helper import (
    with_files,
    CopyFile,
    Symlink,
    AssetSymlink,
    File,
)
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@with_files(
    {
        "readDir": {
            "foo": {},
            "ldir": Symlink("./foo"),
            "bar": File(""),
            "linked": Symlink("./bar"),
        },
        "in.nix": CopyFile("in.nix"),
        "out.exp": AssetSymlink("eval-okay.out.exp"),
        "err.exp": AssetSymlink("eval-okay.err.exp"),
    }
)
def test_read_dir(files: Path, nix: Nix, snapshot: Callable[[str], Snapshot]):
    nix_eval(files, nix, [], snapshot)
