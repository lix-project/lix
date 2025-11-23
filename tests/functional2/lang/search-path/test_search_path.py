from collections.abc import Callable
from pathlib import Path

from functional2.lang.test_lang import test_eval as nix_eval
from functional2.testlib.fixtures.file_helper import AssetSymlink, CopyFile, CopyTree, with_files
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
        "err.exp": AssetSymlink("eval-okay.err.exp"),
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


@with_files(
    {
        "nix-shadow": CopyTree("nix-shadow"),
        "in.nix": CopyFile("in-fetchurl.nix"),
        "out.exp": AssetSymlink("eval-okay-prefixed.out.exp"),
        "err.exp": AssetSymlink("eval-okay-prefixed.err.exp"),
    }
)
def test_prefixed_search_path(files: Path, nix: Nix, snapshot: Callable[[str], Snapshot]):
    nix.env.set_env("NIX_PATH", "nix=nix-shadow")
    nix_eval(files, nix, [], snapshot)


@with_files(
    {
        "nix-shadow": CopyTree("nix-shadow"),
        "in.nix": CopyFile("in-fetchurl.nix"),
        "out.exp": AssetSymlink("eval-okay-prefixless.out.exp"),
        "err.exp": AssetSymlink("eval-okay-prefixless.err.exp"),
    }
)
def test_prefixless_search_path(files: Path, nix: Nix, snapshot: Callable[[str], Snapshot]):
    nix_eval(files, nix, ["-I", "nix-shadow"], snapshot)


@with_files(
    {
        "in.nix": CopyFile("in-fetchurl.nix"),
        "out.exp": AssetSymlink("eval-okay-fetchurl.out.exp"),
        "err.exp": AssetSymlink("eval-okay-fetchurl.err.exp"),
    }
)
def test_empty_search_path(files: Path, nix: Nix, snapshot: Callable[[str], Snapshot]):
    nix_eval(files, nix, [], snapshot)
