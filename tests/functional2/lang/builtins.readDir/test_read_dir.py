from collections.abc import Callable
from pathlib import Path

from lang.test_lang import test_eval_okay as nix_eval
from testlib.fixtures.file_helper import with_files, CopyFile, Symlink, AssetSymlink, File
from testlib.fixtures.nix import Nix
from testlib.fixtures.snapshot import Snapshot


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
