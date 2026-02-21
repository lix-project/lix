from testlib.fixtures.nix import Nix
from testlib.fixtures.command import Command, CommandResult
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.env import ManagedEnv
from testlib.environ import environ
from pathlib import Path
import pytest
from .common import simple_flake, dependent_flake
from collections.abc import Callable
import glob

system = environ.get("system")

files = {"flake-hg1": simple_flake(), "flake-hg2": dependent_flake()}

type Hg = Callable[..., CommandResult]


@pytest.fixture
def hg(env: ManagedEnv, command: Callable[..., Command]) -> Hg:
    env.path.add_program("hg")

    def wrap(*args, **kwargs) -> CommandResult:
        return command(["hg", *args], **kwargs).run().ok()

    return wrap


@pytest.fixture(autouse=True)
def common_init(nix: Nix, files: Path, hg: Hg):
    nix.settings.add_xp_feature("nix-command", "flakes")
    nix.settings.flake_registry = str(nix.env.dirs.test_root / "registry.json")

    hg("init", files / "flake-hg1")
    nix.nix(
        [
            "registry",
            "add",
            "--registry",
            nix.settings.flake_registry,
            "flake1",
            f"hg+file://{files}/flake-hg1",
        ]
    ).run().ok()

    hg("init", files / "flake-hg2")

    hg("add", *glob.glob(str(files / "flake-hg1/*")))  # noqa PTH207: Path.glob *does the wrong thing*
    hg(
        "commit",
        "--config",
        "ui.username=foobar@example.org",
        files / "flake-hg1",
        "-m",
        "Initial commit",
    )

    hg("add", files / "flake-hg2/flake.nix")
    hg(
        "commit",
        "--config",
        "ui.username=foobar@example.org",
        files / "flake-hg2",
        "-m",
        "Initial commit",
    )


@with_files(files)
class TestFlakeHg:
    def test_build(self, nix: Nix, files: Path):
        nix.nix(["build", f"hg+file://{files}/flake-hg2"]).run().ok()
        assert (nix.env.dirs.home / "result/hello").exists()

    def test_metadata_dirty(self, nix: Nix, files: Path):
        metadata = (
            nix.nix(["flake", "metadata", "--json", f"hg+file://{files}/flake-hg2"]).run().json()
        )
        assert "revision" not in metadata

    def test_eval(self, nix: Nix, files: Path):
        result = nix.nix(["eval", f"hg+file://{files}/flake-hg2#expr"]).run().ok()
        assert result.stdout_s == "123\n"

    def test_eval_no_dirty(self, nix: Nix, files: Path):
        result = (
            nix.nix(["eval", f"hg+file://{files}/flake-hg2#expr", "--no-allow-dirty"])
            .run()
            .expect(1)
        )
        assert "is unclean" in result.stderr_s

    class TestLocked:
        @pytest.fixture(autouse=True)
        def init(self, nix: Nix, files: Path, hg: Hg):
            nix.nix(["flake", "lock", f"hg+file://{files}/flake-hg2"]).run().ok()
            hg(
                "commit",
                "--config",
                "ui.username=foobar@example.org",
                files / "flake-hg2",
                "-m",
                "Add lock file",
            )

        def test_metadata_clean(self, nix: Nix, files: Path):
            metadata = (
                nix.nix(["flake", "metadata", "--json", f"hg+file://{files}/flake-hg2"])
                .run()
                .json()
            )
            assert "revision" in metadata

        def test_metadata_refresh(self, nix: Nix, files: Path):
            metadata = (
                nix.nix(
                    ["flake", "metadata", "--json", f"hg+file://{files}/flake-hg2", "--refresh"]
                )
                .run()
                .json()
            )
            assert "revision" in metadata
            assert metadata["revCount"] == 2

        @pytest.mark.parametrize("arg", ["--no-registries", "--no-use-registries"])
        def test_build(self, nix: Nix, files: Path, arg: str):
            nix.nix(["build", f"hg+file://{files}/flake-hg2", arg, "--no-allow-dirty"]).run().ok()
            assert (nix.env.dirs.home / "result/hello").exists()
