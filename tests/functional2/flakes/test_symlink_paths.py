from pathlib import Path

from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import CopyFile, Symlink, with_files, File
from testlib.utils import get_global_asset_pack
from testlib.fixtures.command import Command
from testlib.fixtures.env import ManagedEnv


@with_files(
    {"flake1": {"flake.nix": CopyFile("assets/flake1.nix")}, "flake1_sym": Symlink("flake1")}
)
def test_symlink_points_to_flake(nix: Nix, files: Path) -> None:
    result = nix.nix(["eval", f"{files}/flake1_sym#x"], flake=True).run().ok()
    assert result.stdout_plain == "2"


@with_files(
    {
        "subdir": {"flake1": {"flake.nix": CopyFile("assets/flake1.nix")}},
        "subdir_sym": Symlink("subdir"),
    }
)
def test_symlink_points_to_flake_in_subdir(nix: Nix, files: Path) -> None:
    result = nix.nix(["eval", f"{files}/subdir_sym/flake1#x"], flake=True).run().ok()
    assert result.stdout_plain == "2"


@with_files(
    {
        "flake1": {"subdir": {"flake.nix": CopyFile("assets/flake1.nix")}},
        "flake1_sym": Symlink("flake1/subdir"),
    }
)
def test_symlink_points_to_dir_in_repo(nix: Nix, files: Path) -> None:
    result = nix.nix(["eval", f"{files}/flake1_sym#x"], flake=True).run().ok()
    assert result.stdout_plain == "2"


@with_files(
    {
        "repo1": get_global_asset_pack(".git")
        | {"file": File("Hello"), "subdir": {"flake.nix": CopyFile("assets/flake2.nix")}},
        "repo2": get_global_asset_pack(".git")
        | {"file": File("World"), "flake1_sym": Symlink("../repo1/subdir")},
    }
)
def test_symlink_from_repo_to_another(nix: Nix, files: Path, env: ManagedEnv) -> None:
    env.path.add_program("git")

    Command(["git", "add", "subdir/flake.nix", "file"], _env=env, cwd=files / "repo1").run().ok()
    result = nix.nix(["eval", f"{files}/repo1/subdir#x"], flake=True).run().ok()
    assert result.stdout_plain == '"Hello"'

    Command(["git", "add", "flake1_sym", "file"], _env=env, cwd=files / "repo2").run().ok()
    result = nix.nix(["eval", f"{files}/repo2/flake1_sym#x"], flake=True).run().ok()
    assert result.stdout_plain == '"Hello"'


@with_files(
    {
        "repo1": get_global_asset_pack(".git")
        | {"flake.nix": CopyFile("assets/flake_a.nix"), "subdir": {}},
        "repo2": get_global_asset_pack(".git")
        | {"flake.nix": CopyFile("assets/flake_b.nix"), "subdir_sym": Symlink("../repo1/subdir")},
    }
)
def test_symlink_to_subdir_without_flake(nix: Nix, files: Path, env: ManagedEnv) -> None:
    env.path.add_program("git")

    Command(["git", "add", "flake.nix", "subdir"], _env=env, cwd=files / "repo1").run().ok()
    Command(["git", "add", "flake.nix", "subdir_sym"], _env=env, cwd=files / "repo2").run().ok()

    result = nix.nix(["eval", f"{files}/repo2/subdir_sym#x"], flake=True).run().ok()
    assert result.stdout_plain == '"b"'
