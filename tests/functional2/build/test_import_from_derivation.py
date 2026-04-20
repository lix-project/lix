from collections.abc import Callable

from testlib.fixtures.file_helper import AssetSymlink, CopyFile, with_files
from testlib.fixtures.nix import Nix
from testlib.fixtures.snapshot import Snapshot
from testlib.utils import get_global_asset

import re


def filter_paths(nix: Nix, output: str) -> str:
    return re.sub(
        r"/nix/store/[0-9a-z]{32}",
        "/nix/store/hashfilteredforrepeatableoutputs",
        output.replace(nix.env.dirs.test_root.as_posix(), ""),
    )


@with_files(
    {
        "import-derivation.nix": CopyFile(
            "assets/test_import_from_derivation/import-derivation.nix"
        ),
        "config.nix": get_global_asset("config.nix"),
        "error": AssetSymlink("assets/test_import_from_derivation/warn-ifd.err.exp"),
    }
)
def test_warn_ifd(nix: Nix, snapshot: Callable[[str], Snapshot]):
    error = (
        nix.nix_build(
            [
                "./import-derivation.nix",
                "--no-out-link",
                *["--option", "warn-import-from-derivation", "true"],
            ]
        )
        .run()
        .ok()
        .stderr_plain
    )

    assert snapshot("error") == filter_paths(nix, error)


@with_files(
    {
        "import-derivation.nix": CopyFile(
            "assets/test_import_from_derivation/import-derivation.nix"
        ),
        "config.nix": get_global_asset("config.nix"),
        "error": AssetSymlink("assets/test_import_from_derivation/deny-ifd.err.exp"),
    }
)
def test_deny_ifd(nix: Nix, snapshot: Callable[[str], Snapshot]):
    error = (
        nix.nix_build(
            [
                "./import-derivation.nix",
                "--no-out-link",
                *["--option", "allow-import-from-derivation", "false"],
            ]
        )
        .run()
        .expect(1)
        .stderr_plain
    )
    assert snapshot("error") == filter_paths(nix, error)


@with_files(
    {
        "import-derivation.nix": CopyFile(
            "assets/test_import_from_derivation/import-derivation.nix"
        ),
        "config.nix": get_global_asset("config.nix"),
        "error": AssetSymlink("assets/test_import_from_derivation/allow-ifd.err.exp"),
    }
)
def test_allow_ifd(nix: Nix, snapshot: Callable[[str], Snapshot]):
    build = nix.nix_build(["./import-derivation.nix", "--no-out-link"]).run().ok()
    error = build.stderr_plain
    out_path = build.stdout_plain

    assert nix.physical_store_path_for(out_path).read_text() == "FOO579"
    assert snapshot("error") == filter_paths(nix, error)


@with_files(
    {
        "import-derivation.nix": CopyFile(
            "assets/test_import_from_derivation/import-derivation.nix"
        ),
        "config.nix": get_global_asset("config.nix"),
        "error": AssetSymlink(
            "assets/test_import_from_derivation/instantiate-ifd-readonly-fail.err.exp"
        ),
    }
)
def test_instantiate_ifd_readonly_fail(nix: Nix, snapshot: Callable[[str], Snapshot]):
    error = (
        nix.nix_instantiate(["--readonly-mode", "./import-derivation.nix"])
        .run()
        .expect(1)
        .stderr_plain
    )
    assert snapshot("error") == filter_paths(nix, error)
