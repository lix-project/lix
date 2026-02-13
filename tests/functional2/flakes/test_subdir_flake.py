from fnmatch import fnmatch
from testlib.fixtures.git import Git
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from pathlib import Path
from .common import simple_flake


@with_files({"flake-container": get_global_asset_pack(".git") | {"flake-dir": simple_flake()}})
def test_subdir_flake(nix: Nix, files: Path, git: Git):
    container = files / "flake-container"
    flake = container / "flake-dir"

    nix.settings.add_xp_feature("nix-command", "flakes")

    git(container, "add", "flake-dir")

    info = nix.nix(["flake", "info", "--json"], cwd=flake).run().json()

    assert fnmatch(info["resolvedUrl"], "git+file://*/flake-container[?]dir=flake-dir")
    assert fnmatch(info["url"], "git+file://*/flake-container[?]dir=flake-dir")

    nix.nix(["build", f"path:{flake}#foo", "-L"]).run().ok()
