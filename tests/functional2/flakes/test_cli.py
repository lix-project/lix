from pathlib import Path
import pytest
import re
import tarfile

from testlib.fixtures.nix import Nix
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.git import Git
from testlib.utils import get_global_asset_pack
from testlib.fixtures.file_helper import with_files, File, FileDeclaration, _init_files  # noqa: PLC2701
from testlib.environ import environ
from .common import simple_flake, dependent_flake

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
flake3_files = {
    "flake3": get_global_asset_pack(".git")
    | {
        "flake.nix": File(f"""{{
          description = "Fnord";

          outputs = {{ self, flake2 }}: rec {{
            packages.{system}.xyzzy = flake2.packages.{system}.bar;

            checks.xyzzy = packages.{system}.xyzzy;
          }};
        }}"""),
        "default.nix": File("{ x = 123; }"),
    }
}
flake5_files = {"flake5": dependent_flake()}


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


def _modify_flake1(flake1: Path, git: Git | None):
    (flake1 / "foo").write_text("foo")
    (flake1 / "flake.nix").write_text((flake1 / "flake.nix").read_text() + "# foo")
    if git is not None:
        git(flake1, "add", "foo")
        git(flake1, "commit", "-a", "-m", "Foo")


@pytest.fixture
def flake2(git: Git, env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    return _make_flake_repo("flake2", flake2_files, git, env, request)


@pytest.fixture
def flake3(git: Git, env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    return _make_flake_repo("flake3", flake3_files, git, env, request)


@pytest.fixture
def flake5(env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    _init_files(flake5_files, env.dirs.test_root, request.path.parent, env)
    return env.dirs.test_root / "flake5"


@pytest.fixture
def flake5_locked_tarball(env: ManagedEnv, flake5: Path, nix: Nix) -> Path:
    nix.nix(["flake", "lock", flake5]).run().ok()
    path = env.dirs.test_root / "flake5.tar.gz"
    with tarfile.open(path, "w:gz") as tar:
        tar.add(flake5, "flake5")
    return path


@pytest.fixture
def registry(nix: Nix, flake1: Path, flake2: Path, flake3: Path) -> Path:
    registry = nix.env.dirs.test_root / "registry.json"
    nix.settings.flake_registry = str(registry)

    # Construct a custom registry, additionally test the --registry flag
    nix.nix(
        ["registry", "add", "--registry", registry, "flake1", f"git+file://{flake1}"]
    ).run().ok()
    nix.nix(
        ["registry", "add", "--registry", registry, "flake2", f"git+file://{flake2}"]
    ).run().ok()
    nix.nix(
        ["registry", "add", "--registry", registry, "flake3", f"git+file://{flake3}"]
    ).run().ok()
    nix.nix(["registry", "add", "--registry", registry, "flake4", "flake3"]).run().ok()
    nix.nix(["registry", "add", "--registry", registry, "nixpkgs", "flake1"]).run().ok()

    return registry


@pytest.mark.usefixtures("registry")
class TestBuild:
    def test_bare_repo(self, nix: Nix, flake1: Path, git: Git):
        git(None, "clone", "--bare", flake1, "bare")
        logs = nix.nix(["build", f"git+file://{nix.env.dirs.home}/bare"]).run().ok().stderr_s
        assert re.search(r"building '.*-simple.drv'", logs)

    def test_tarball(self, nix: Nix, flake5_locked_tarball: Path):
        logs = nix.nix(["build", f"file://{flake5_locked_tarball}"]).run().ok().stderr_s
        assert "fetching tarball input" in logs
        assert re.search(r"building '.*-simple.drv'", logs)

    def test_tarball_with_sri(self, nix: Nix, flake5_locked_tarball: Path):
        # lockfile contains absolute references to test data, hash can't be deterministic
        url = (
            nix.nix(["flake", "metadata", "--json", f"file://{flake5_locked_tarball}"])
            .run()
            .json()["url"]
        )
        assert "sha256-" in url

        nix.clear_store()

        logs = nix.nix(["build", url]).run().ok().stderr_s
        assert "fetching tarball input" in logs
        assert re.search(r"building '.*-simple.drv'", logs)

    def test_tarball_bad_sri(self, nix: Nix, flake5_locked_tarball: Path):
        url = f"file://{flake5_locked_tarball}?narHash=sha256-qQ2Zz4DNHViCUrp6gTS7EE4+RMqFQtUfWF2UNUtJKS0="
        logs = nix.nix(["build", url]).run().expect(102).stderr_s
        assert "NAR hash mismatch" in logs


@pytest.mark.usefixtures("registry")
class TestLock:
    def test_path_url(self, nix: Nix, flake5: Path):
        logs = nix.nix(["flake", "lock", f"path://{flake5}"]).run().ok().stderr_s
        assert "Added input 'flake1'" in logs
        assert f"fetching path input 'path:{flake5}" in logs


@pytest.mark.usefixtures("registry")
class TestMetadata:
    def test_registry(self, nix: Nix):
        metadata = nix.nix(["flake", "metadata", "flake1"]).run().ok().stdout_s
        assert re.search(r"Locked URL:.*flake1.*", metadata)

    @pytest.mark.parametrize("source", [[], ["."]])
    def test_cwd(self, nix: Nix, flake1: Path, source: list[str]):
        metadata = nix.nix(["flake", "metadata", *source], cwd=flake1).run().ok().stdout_s
        assert re.search(r"Locked URL:.*flake1.*", metadata)

    def test_absolute(self, nix: Nix, flake1: Path):
        metadata = nix.nix(["flake", "metadata", flake1]).run().ok().stdout_s
        assert re.search(r"Locked URL:.*flake1.*", metadata)

    @pytest.mark.parametrize("source", ["flake1", None])
    def test_json(self, nix: Nix, flake1: Path, source: str | None, git: Git):
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        metadata = nix.nix(["flake", "metadata", source or flake1, "--json"]).run().json()
        assert metadata["description"] == "Bla bla"
        assert Path(metadata["path"]).is_dir()
        assert metadata["lastModified"] == 23
        assert metadata["revision"] == flake1_original_commit

    def test_json_registry_dirty(self, nix: Nix, flake1: Path, git: Git):
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        (flake1 / "foo").write_text("foo")
        git(flake1, "add", "foo")
        metadata = nix.nix(["flake", "metadata", "flake1", "--json"]).run().json()
        assert metadata["dirtyRevision"] == f"{flake1_original_commit}-dirty"

        _modify_flake1(flake1, git)
        flake1_modified_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        metadata = nix.nix(["flake", "metadata", "flake1", "--json", "--refresh"]).run().json()
        assert metadata["revision"] == flake1_modified_commit
        assert "dirtyRevision" not in metadata


class TestAddPath:
    @with_files({"badFlake": {"flake.nix": File("INVALID")}})
    def test_does_not_eval(self, nix: Nix, files: Path):
        nix.nix(["store", "add-path", str(files / "badFlake")]).run().ok()

    def test_add_flake(self, nix: Nix, flake1: Path):
        path = nix.nix(["store", "add-path", flake1]).run().ok().stdout_s.strip()

        info = nix.nix(["path-info", path]).run().ok().stdout_s
        assert "flake1" in info

        # required to the path: test, otherwise outputs are not valid
        nix.nix(["build", flake1]).run().ok()
        info = nix.nix(["path-info", f"path:{path}"]).run().ok().stdout_s
        assert "simple" in info


@pytest.mark.usefixtures("registry")
class TestLegacyCLI:
    def test_instantiate_indirect(self, nix: Nix):
        assert (
            nix.nix_instantiate(["--eval", "flake:flake3", "-A", "x"]).run().ok().stdout_s
            == "123\n"
        )

    def test_instantiate_url(self, nix: Nix, flake3: Path):
        assert (
            nix.nix_instantiate(["--eval", f"flake:git+file://{flake3}", "-A", "x"])
            .run()
            .ok()
            .stdout_s
            == "123\n"
        )

    def test_instantiate_nix_path_option(self, nix: Nix):
        assert (
            nix.nix_instantiate(["-I", "flake3=flake:flake3", "--eval", "<flake3>", "-A", "x"])
            .run()
            .ok()
            .stdout_s
            == "123\n"
        )

    def test_instantiate_nix_path_env(self, nix: Nix):
        nix.env["NIX_PATH"] = "flake3=flake:flake3"
        assert nix.nix_instantiate(["--eval", "<flake3>", "-A", "x"]).run().ok().stdout_s == "123\n"


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
