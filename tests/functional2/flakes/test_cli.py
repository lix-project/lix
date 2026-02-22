from pathlib import Path
import pytest

from testlib.fixtures.nix import Nix
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.git import Git
from testlib.utils import get_global_asset_pack
from testlib.fixtures.file_helper import File, FileDeclaration, _init_files  # noqa: PLC2701
from testlib.environ import environ
from .common import simple_flake

system = environ.get("system")


def dotgit_other_branch() -> FileDeclaration:
    result = get_global_asset_pack(".git")
    result[".git"]["HEAD"] = File("ref: refs/heads/other-branch")
    return result


@pytest.fixture(autouse=True)
def setup_env(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


flake1_files = {"flake1": get_global_asset_pack(".git") | simple_flake()}
flake2_files = {
    "flake2": dotgit_other_branch()
    | {
        "flake.nix": File(f"""{{
          description = "Fnord";

          outputs = {{ self, flake1 }}: rec {{
            packages.{system}.bar = flake1.packages.{system}.foo;
          }};
        }}""")
    }
}


def _make_flake_repo(
    name: str, files: FileDeclaration, git: Git, env: ManagedEnv, request: pytest.FixtureRequest
) -> Path:
    _init_files(files, env.dirs.test_root, request.path.parent, env)
    repo = env.dirs.test_root / name
    git(repo, "add", ".")
    git(repo, "commit", "-m", "Initial")
    return repo


@pytest.fixture
def flake1(git: Git, env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    return _make_flake_repo("flake1", flake1_files, git, env, request)


@pytest.fixture
def flake2(git: Git, env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    return _make_flake_repo("flake2", flake2_files, git, env, request)


@pytest.fixture
def registry(nix: Nix, flake1: Path, flake2: Path) -> Path:  # noqa: ARG001
    registry = nix.env.dirs.test_root / "registry.json"
    nix.settings.flake_registry = str(registry)

    # Construct a custom registry, additionally test the --registry flag
    nix.nix(
        ["registry", "add", "--registry", registry, "flake1", f"git+file://{flake1}"]
    ).run().ok()

    return registry


class TestAlternateLockFiles:
    @pytest.fixture(autouse=True)
    def init(self, nix: Nix, flake1: Path, flake2: Path, registry: Path, git: Git):  # noqa: ARG002
        git(flake1, "commit", "--allow-empty", "-m", "to change branch head")
        nix.nix(["flake", "lock", flake2]).run().ok()

    def test_other_lockfile_is_identical(self, nix: Nix, flake2: Path):
        nix.nix(["flake", "lock", flake2, "--output-lock-file", "flake2.lock"]).run().ok()
        assert (nix.env.dirs.home / "flake2.lock").read_text() == (
            flake2 / "flake.lock"
        ).read_text()

    def test_input_overrides_change_lockfiles(self, nix: Nix, flake1: Path, flake2: Path, git: Git):
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        nix.nix(
            [
                "flake",
                "lock",
                flake2,
                "--output-lock-file",
                "flake2.lock",
                "--override-input",
                "flake1",
                f"git+file://{flake1}?rev={flake1_original_commit}",
            ]
        ).run().ok()
        assert (nix.env.dirs.home / "flake2.lock").read_text() != (
            flake2 / "flake.lock"
        ).read_text()
        assert (
            flake1_original_commit
            in nix.nix(["flake", "metadata", flake2, "--reference-lock-file", "flake2.lock"])
            .run()
            .ok()
            .stdout_s
        )


def test_reference_lock_file_requires_allow_dirty(nix: Nix, flake2: Path):
    assert (
        "error: reference lock file was provided, but the `allow-dirty` setting is set to false"
        in nix.nix(
            [
                "flake",
                "metadata",
                flake2,
                "--no-allow-dirty",
                "--reference-lock-file",
                f"{nix.env.dirs.home}/flake2-overridden.lock",
            ]
        )
        .run()
        .expect(1)
        .stderr_s
    )
