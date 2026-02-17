from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, CopyTree, AssetSymlink
from testlib.fixtures.snapshot import Snapshot
from pathlib import Path
import os


@with_files(
    {
        "flake": CopyTree("assets/metadata/flake"),
        "metadata.out": AssetSymlink("assets/metadata/metadata.out"),
    }
)
def test_metadata(nix: Nix, files: Path, snapshot: Snapshot):
    nix.settings.add_xp_feature("nix-command", "flakes")
    nix.env.dirs.nix_store_dir = "/nix/store"
    nix.env["TZ"] = "UTC"
    nix.env["LC_ALL"] = "C.UTF-8"

    os.utime(files / "flake/flake.nix", (1000, 1000))
    os.utime(files / "flake/flake.lock", (1000, 1000))
    os.utime(files / "flake", (1000, 1000))

    # We use NIX_STORE_DIR which causes unstable paths. This is goofy. We can
    # just use `--store` which sets `rootDir` and does not have this problem.
    result = (
        nix.nix(["flake", "metadata", "--store", nix.env.dirs.real_store_dir, files / "flake"])
        .run()
        .ok()
    )
    assert snapshot("metadata.out") == result.stdout_s.replace(str(files), "$root")
