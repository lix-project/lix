from collections.abc import Callable
from pathlib import Path

from functional2.lang.test_lang import test_eval as nix_eval
from functional2.testlib.fixtures.file_helper import with_files, CopyTree, CopyFile, AssetSymlink
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@with_files(
    {
        "dir1": CopyTree("dir1"),
        "dir2": CopyTree("dir2"),
        "dir3": CopyTree("dir3"),
        "dir4": CopyTree("dir4"),
        "lib.nix": CopyFile("../lib.nix"),
        "in.nix": CopyFile("in.nix"),
        "out.exp": AssetSymlink("eval-okay.out.exp"),
    }
)
def test_search_path(files: Path, nix: Nix, snapshot: Callable[[str], Snapshot]):
    nix.env.set_env("NIX_PATH", "dir3:dir4")
    nix_eval(
        files,
        nix,
        [
            "--extra-deprecated-features",
            "shadow-internal-symbols",
            "-I",
            "dir1",
            "-I",
            "dir2",
            "-I",
            "dir5=dir3",
        ],
        snapshot,
    )
