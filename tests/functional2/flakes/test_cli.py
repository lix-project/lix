from pathlib import Path
import pytest
import re
import tarfile
import json
import shutil

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
nonflake_files = {"nonFlake": get_global_asset_pack(".git") | {"README.md": File("FNORD")}}


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
def flake3_locked(git: Git, env: ManagedEnv, nix: Nix, flake3: Path, nonflake: Path) -> Path:  # noqa: ARG001
    nix.nix(["flake", "lock", flake3, "--commit-lock-file"]).run().ok()
    return flake3


@pytest.fixture
def flake3_medium(git: Git, env: ManagedEnv, nix: Nix, flake3: Path, nonflake: Path) -> Path:  # noqa: ARG001
    (flake3 / "flake.nix").write_text(f"""{{
      inputs = {{
        nonFlake = {{
          url = "{env.dirs.test_root}/nonFlake";
          flake = false;
        }};
      }};

      description = "Fnord";

      outputs = {{ self, flake1, flake2, nonFlake }}: rec {{
        packages.{system}.sth = flake1.packages.{system}.foo;
        packages.{system}.fnord =
          with import ./config.nix;
          mkDerivation {{
            inherit system;
            name = "fnord";
            buildCommand = ''
              cat ${{nonFlake}}/README.md > $out
            '';
          }};
      }};
    }}""")
    nix.nix(["flake", "lock", flake3]).run().ok()
    git(flake3, "add", "flake.nix", "flake.lock")
    git(flake3, "commit", "-m", "medium config")
    return flake3


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
def nonflake(env: ManagedEnv, request: pytest.FixtureRequest) -> Path:
    _init_files(nonflake_files, env.dirs.test_root, request.path.parent, env)
    return env.dirs.test_root / "nonFlake"


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
class TestAttrMatch:
    def test_fuzzy_plain(self, nix: Nix):
        logs = nix.nix(["eval", "flake1#ERROR"]).run().expect(1).stderr_s
        assert re.search(r"error:.*does not provide attribute.*or 'ERROR'$", logs)

    def test_exact_plain(self, nix: Nix):
        logs = nix.nix(["eval", "flake1#.ERROR"]).run().expect(1).stderr_s
        assert re.search(r"error:.*does not provide attribute 'ERROR'$", logs)

    def test_fuzzy_branch(self, nix: Nix):
        path = nix.nix(["eval", "flake1/main#foo"]).run().ok().stdout_s
        assert "-simple.drv" in path

    class TestRegistryBranch:
        @pytest.fixture(autouse=True)
        def create_other_branch(self, flake1: Path, git: Git):
            git(flake1, "checkout", "-b", "other")
            (flake1 / "flake.nix").write_text(
                (flake1 / "flake.nix").read_text().replace("foo", "bar")
            )
            git(flake1, "commit", "-a", "-mrename foo -> bar")
            git(flake1, "checkout", "main")

        def test_fuzzy_main_branch(self, nix: Nix):
            assert (
                "flake 'flake:flake1/main' does not provide attribute"
                in nix.nix(["eval", "flake1/main#bar"]).run().expect(1).stderr_s
            )
            path = nix.nix(["eval", "flake1/main#foo"]).run().ok().stdout_s
            assert "-simple.drv" in path

        def test_fuzzy_other_branch(self, nix: Nix):
            assert (
                "flake 'flake:flake1/other' does not provide attribute"
                in nix.nix(["eval", "flake1/other#foo"]).run().expect(1).stderr_s
            )
            path = nix.nix(["eval", "flake1/other#bar"]).run().ok().stdout_s
            assert "-simple.drv" in path


def test_eval_system_takes_effect(nix: Nix, flake1: Path):
    (flake1 / "flake.nix").write_text(
        (flake1 / "flake.nix").read_text().replace(system, "kitty-kitty")
    )
    nix.nix(["build", "--eval-system", "kitty-kitty", flake1]).run().ok()


class TestGetFlake:
    def test_unlocked_pure_fails(self, nix: Nix, flake1: Path):
        expr = f'(builtins.getFlake "{flake1}").packages.{system}.default'
        result = nix.nix(["build", "--expr", expr]).run().expect(1)
        assert "error: cannot call 'getFlake' on unlocked flake reference" in result.stderr_s

    def test_unlocked_impure_works(self, nix: Nix, flake1: Path):
        expr = f'(builtins.getFlake "{flake1}").packages.{system}.default'
        nix.nix(["build", "--impure", "--expr", expr]).run().ok()
        assert (Path(nix.env.dirs.home / "result/hello")).exists()

    def test_locked_url_pure_works(self, nix: Nix, flake1: Path, git: Git):
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        expr = f'(builtins.getFlake "git+file://{flake1}?rev={flake1_original_commit}").packages.{system}.default'
        nix.nix(["build", "--expr", expr]).run().ok()
        assert (Path(nix.env.dirs.home / "result/hello")).exists()

    def test_unlocked_dep_pure_fails(self, nix: Nix, flake2: Path):
        # NOTE: this is the same test as test_unlocked_dep_pure_fails because flake2 is not locked,
        # and flake2 *cannot* be locked because that would also lock flake1 in the flake2 lock file
        nix.nix(["eval", "--expr", f'(builtins.getFlake "{flake2}")']).run().expect(1)

    @pytest.mark.usefixtures("registry")
    def test_unlocked_dep_impure_works(self, nix: Nix, flake2: Path):
        # NOTE: see test above, same thing happens here
        nix.nix(["eval", "--impure", "--expr", f'(builtins.getFlake "{flake2}")']).run().ok()


@pytest.mark.usefixtures("registry")
class TestRegistry:
    def test_registry_list(self, nix: Nix):
        result = nix.nix(["registry", "list"]).run().ok().stdout_s
        assert len(result.splitlines()) == 5
        assert re.search(r"^global", result, re.M)
        assert not re.search(r"^user", result, re.M)  # nothing in user registry

    def test_registry_caching(self, nix: Nix, registry: Path):
        result = (
            nix.nix(["registry", "list", "--flake-registry", f"file://{registry}"])
            .run()
            .ok()
            .stdout_s
        )
        assert "flake3" in result
        registry.unlink()
        nix.nix(["store", "gc"]).run().ok()
        result = (
            nix.nix(["registry", "list", "--flake-registry", f"file://{registry}"])
            .run()
            .ok()
            .stdout_s
        )
        assert "flake3" in result

    class TestCLI:
        def test_add(self, nix: Nix):
            nix.nix(["registry", "add", "flake1", "flake3"]).run().ok()
            result = nix.nix(["registry", "list"]).run().ok().stdout_s
            assert len(result.splitlines()) == 6
            assert re.search(r"^global", result, re.M)
            assert re.search(r"^user", result, re.M)  # something in user registry now

        def test_pin_rev(self, nix: Nix, flake1: Path, git: Git):
            commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
            nix.nix(["registry", "pin", "flake1"]).run().ok()
            result = nix.nix(["registry", "list"]).run().ok().stdout_s
            assert f"flake1?ref=refs/heads/main&rev={commit}" in result

        def test_pin_to_other(self, nix: Nix, flake3: Path, git: Git):
            commit = git(flake3, "rev-parse", "HEAD").stdout_s.strip()
            nix.nix(["registry", "pin", "flake1", "flake3"]).run().ok()
            result = nix.nix(["registry", "list"]).run().ok().stdout_s
            assert f"flake3?ref=refs/heads/main&rev={commit}" in result

        def test_add_remove(self, nix: Nix):
            nix.nix(["registry", "add", "flake1", "flake3"]).run().ok()
            nix.nix(["registry", "remove", "flake1"]).run().ok()
            result = nix.nix(["registry", "list"]).run().ok().stdout_s
            assert len(result.splitlines()) == 5
            assert re.search(r"^global", result, re.M)
            assert not re.search(r"^user", result, re.M)  # nothing in user registry

        class TestShorthands:
            """
            'nix registry add' should accept flake shorthands (with or without branch or rev)
            in the from argument, but reject fully-qualified from-urls (direct or indirect).
            """

            def test_plain(self, nix: Nix):
                nix.nix(["registry", "add", "nixpkgz", "github:NixOS/nixpkgz"]).run().ok()
                assert "user   flake:nixpkgz" in nix.nix(["registry", "list"]).run().ok().stdout_s
                nix.nix(["registry", "remove", "nixpkgz"]).run().ok()
                assert (
                    "user   flake:nixpkgz" not in nix.nix(["registry", "list"]).run().ok().stdout_s
                )

            def test_branch(self, nix: Nix):
                nix.nix(["registry", "add", "nixpkgz/branch", "github:NixOS/nixpkgz"]).run().ok()
                assert (
                    "user   flake:nixpkgz/branch"
                    in nix.nix(["registry", "list"]).run().ok().stdout_s
                )
                nix.nix(["registry", "remove", "nixpkgz/branch"]).run().ok()
                assert (
                    "user   flake:nixpkgz/branch"
                    not in nix.nix(["registry", "list"]).run().ok().stdout_s
                )

            def test_branch_rev(self, nix: Nix):
                ref = "nixpkgz/branch/1db42b7fe3878f3f5f7a4f2dc210772fd080e205"
                nix.nix(["registry", "add", ref, "github:NixOS/nixpkgz"]).run().ok()
                assert f"user   flake:{ref}" in nix.nix(["registry", "list"]).run().ok().stdout_s
                nix.nix(["registry", "remove", ref]).run().ok()
                assert (
                    f"user   flake:{ref}" not in nix.nix(["registry", "list"]).run().ok().stdout_s
                )

            def test_rejects_qualified(self, nix: Nix):
                assert (
                    "error: 'from-url' argument must be a shorthand"
                    in nix.nix(["registry", "add", "flake:nixpkgz", "github:NixOS/nixpkgz"])
                    .run()
                    .expect(1)
                    .stderr_s
                )
                assert (
                    "error: 'from-url' argument must be a shorthand"
                    in nix.nix(["registry", "add", "github:NixOS/nixpkgz", "github:NixOS/nixpkgz"])
                    .run()
                    .expect(1)
                    .stderr_s
                )

        def test_disabled_global_registry(self, nix: Nix, flake1: Path, flake2: Path):
            nix.nix(["registry", "add", "user-flake1", f"git+file://{flake1}"]).run().ok()
            nix.nix(["registry", "add", "user-flake2", f"git+file://{flake2}"]).run().ok()

            result = nix.nix(["--flake-registry", "", "registry", "list"]).run().ok().stdout_s
            assert len(result.splitlines()) == 2
            assert not re.search(r"^global", result, re.M)  # nothing in global registry
            assert re.search(r"^user", result, re.M)


@pytest.mark.usefixtures("registry")
class TestBuild:
    def test_build_attr(self, nix: Nix):
        nix.nix(["build", "flake1#foo"]).run().ok()
        assert (nix.env.dirs.home / "result/hello").exists()

    def test_build_without_attr_registry(self, nix: Nix):
        nix.nix(["build", "flake1"]).run().ok()
        assert (nix.env.dirs.home / "result/hello").exists()

    @pytest.mark.parametrize("scheme", ["", "git+file://"])
    def test_build_without_attr_url(self, nix: Nix, flake1: Path, scheme: str):
        nix.nix(["build", f"{scheme}{flake1}"]).run().ok()
        assert (nix.env.dirs.home / "result/hello").exists()

    @pytest.mark.parametrize("arg", ["--no-registries", "--no-use-registries"])
    def test_pure_build_unlocked_deps_failure(self, nix: Nix, arg: str):
        result = nix.nix(["build", "flake2#bar", arg]).run().expect(1).stderr_s
        assert (
            "error: 'flake:flake2' is an indirect flake reference, but registry lookups are not allowed"
            in result
        )

    def test_impure_build_unlocked_deps_fails(self, nix: Nix):
        result = nix.nix(["build", "flake2#bar", "--impure"]).run().expect(1).stderr_s
        assert "error: cannot write modified lock file" in result

    def test_impure_build_unlocked_deps_succeeds_with_no_write(self, nix: Nix):
        logs = (
            nix.nix(["build", "flake2#bar", "--impure", "--no-write-lock-file"]).run().ok().stderr_s
        )
        assert re.search(r"building '.*-simple.drv'", logs)

    def test_build_unlocked_fails_with_no_update(self, nix: Nix, flake2: Path):
        logs = nix.nix(["build", f"{flake2}#bar", "--no-update-lock-file"]).run().expect(1).stderr_s
        assert "requires lock file changes" in logs

    def test_build_unlocked_succeeds_with_no_write(self, nix: Nix):
        logs = nix.nix(["build", "flake2#bar", "--no-write-lock-file"]).run().ok().stderr_s
        assert re.search(r"building '.*-simple.drv'", logs)

    class TestLockedFlake2:
        @pytest.fixture(autouse=True)
        def lock_flake2(self, nix: Nix, flake2: Path, registry: Path):  # noqa: ARG002
            nix.nix(["flake", "lock", flake2, "--commit-lock-file"]).run().ok()

        def test_setup_did_commit(self, flake2: Path, git: Git):
            assert (flake2 / "flake.lock").exists()
            assert not git(flake2, "diff", "HEAD").stdout_s

        def test_rerunning_builds_does_not_change_lockfile(self, nix: Nix, flake2: Path, git: Git):
            nix.nix(["build", f"{flake2}#bar", "--no-write-lock-file"]).run().ok()
            assert not git(flake2, "diff", "HEAD").stdout_s

        @pytest.mark.parametrize(
            "args",
            [
                ["--flake-registry", "file:///no-registry.json", "--refresh"],
                ["--no-registries", "--refresh"],
                ["--no-use-registries", "--refresh"],
                [],
            ],
        )
        @pytest.mark.parametrize("scheme", ["", "git+file://"])
        def test_locked_build_works(self, nix: Nix, flake2: Path, args: list[str], scheme: str):
            # registry fetches are not logged anywhere!
            nix.nix(["build", *args, f"{scheme}{flake2}#bar"]).run().ok()

        def test_lock_idempotent(self, nix: Nix, flake2: Path, git: Git):
            nix.nix(["flake", "lock", flake2]).run().ok()
            assert not git(flake2, "diff", "HEAD").stdout_s

    def test_indirect_dependencies(self, nix: Nix, flake3: Path):
        logs = nix.nix(["build", f"{flake3}#xyzzy"]).run().ok().stderr_s
        assert "Added input 'flake2'" in logs
        assert "Added input 'flake2/flake1'" in logs
        assert re.search(r"building '.*-simple.drv'", logs)

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

    class TestIncompleteLockfile:
        @pytest.fixture(autouse=True)
        def update_flake(self, nix: Nix, flake3: Path, git: Git, registry: Path):  # noqa: ARG002
            nix.nix(["flake", "lock", flake3]).run().ok()
            git(flake3, "add", "flake.lock")
            (flake3 / "flake.nix").write_text(f"""{{
                outputs = {{ self, flake1, flake2 }}: rec {{
                  packages.{system}.xyzzy = flake2.packages.{system}.bar;
                  packages.{system}."sth sth" = flake1.packages.{system}.foo;
                }};
            }}""")
            git(flake3, "add", "flake.nix")
            git(flake3, "commit", "-m", "Update flake.nix")

        @pytest.mark.parametrize("attr", ["sth sth", "sth%20sth"])
        def test_build(self, nix: Nix, flake3: Path, git: Git, attr: str):
            nix.nix(["build", f"{flake3}#{attr}"]).run().ok()

            # check that the lockfile was changed
            assert git(flake3, "diff", "HEAD").stdout_s


@pytest.mark.usefixtures("registry")
def test_flakes_gcroots(nix: Nix, flake1: Path, flake2: Path):
    """
    Test whether flakes are registered as GC roots for offline use.
    FIXME(ancient): use tarballs rather than git.
    """

    nix.env["_NIX_FORCE_HTTP"] = "1"
    shutil.rmtree(nix.env.dirs.home / ".cache")
    nix.clear_store()  # absolutely make sure the store is empty first

    nix.nix(["flake", "lock", flake2, "--commit-lock-file"]).run().ok()
    nix.nix(["build", f"git+file://{flake2}#bar"]).run().ok()

    shutil.rmtree(flake1)
    shutil.rmtree(flake2)

    nix.nix(["store", "gc"]).run().ok()

    nix.nix(["build", f"git+file://{flake2}#bar"]).run().ok()
    nix.nix(["build", f"git+file://{flake2}#bar", "--refresh"]).run().ok()


@pytest.mark.usefixtures("registry")
def test_flake_clone(nix: Nix):
    dest = nix.env.dirs.home / "flake1-v2"
    nix.nix(["flake", "clone", "flake1", "--dest", dest]).run().ok()
    assert (dest / "flake.nix").exists()


@pytest.mark.usefixtures("registry")
class TestLock:
    def test_path_url(self, nix: Nix, flake5: Path):
        logs = nix.nix(["flake", "lock", f"path://{flake5}"]).run().ok().stderr_s
        assert "Added input 'flake1'" in logs
        assert f"fetching path input 'path:{flake5}" in logs

    def test_override_inputs(
        self, nix: Nix, flake1: Path, flake3: Path, flake5_locked_tarball: Path, git: Git
    ):
        # this is one big test due to necessary state changes, 🐻 with me
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()

        url = f"file://{flake5_locked_tarball}"
        nix.nix(["flake", "lock", flake3, "--override-input", "flake2/flake1", url]).run().ok()
        lock = json.loads((flake3 / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["url"] == url

        nix.nix(["flake", "lock", flake3, "--override-input", "flake2/flake1", "flake1"]).run().ok()
        lock = json.loads((flake3 / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["rev"] == flake1_original_commit

        git(flake1, "checkout", "-b", "other")
        _modify_flake1(flake1, git)
        other_hash = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        git(flake1, "checkout", "main")

        ref = f"flake1/other/{other_hash}"
        nix.nix(["flake", "lock", flake3, "--override-input", "flake2/flake1", ref]).run().ok()
        lock = json.loads((flake3 / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["rev"] == other_hash

        nix.nix(["flake", "lock", flake3]).run().ok()
        lock = json.loads((flake3 / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["rev"] == other_hash

    def test_update_individual_input(self, nix: Nix, flake1: Path, flake3_medium: Path, git: Git):
        flake1_original_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()
        _modify_flake1(flake1, git)
        flake1_modified_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()

        nix.nix(["flake", "update", "flake2/flake1", "--flake", flake3_medium]).run().ok()
        lock = json.loads((flake3_medium / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["rev"] == flake1_original_commit
        assert lock["nodes"]["flake1_2"]["locked"]["rev"] == flake1_modified_commit

    def test_update_multiple_inputs(self, nix: Nix, flake1: Path, flake3_medium: Path, git: Git):
        _modify_flake1(flake1, git)
        flake1_modified_commit = git(flake1, "rev-parse", "HEAD").stdout_s.strip()

        nix.nix(["flake", "update", "flake1", "flake2/flake1", "--flake", flake3_medium]).run().ok()
        lock = json.loads((flake3_medium / "flake.lock").read_text())
        assert lock["nodes"]["flake1"]["locked"]["rev"] == flake1_modified_commit
        assert lock["nodes"]["flake1_2"]["locked"]["rev"] == flake1_modified_commit


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
