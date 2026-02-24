import dataclasses
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent
from collections.abc import Callable

import pytest
from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.nix import Nix
from testlib.fixtures.command import Command


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command")
    nix.env.path.add_program("hg")


@dataclass
class HgRepo:
    path: Path
    rev1: str
    rev2: str

    @property
    def uri(self) -> str:
        # Intentionally not in a canonical form
        # See https://github.com/NixOS/nix/issues/6195
        return f'"file://{self.path.parent}/./{self.path.name}"'


@pytest.fixture(scope="session")
def static_hg_repo(tmp_path_factory: pytest.TempPathFactory) -> HgRepo:
    """
    create a static mercurial repo with our default settings because hg is *so slow*
    """
    repo = tmp_path_factory.mktemp("hg")

    subprocess.check_call(["hg", "init", repo])
    (repo / ".hg/hgrc").write_text(
        dedent("""
        [ui]
        username = Foobar <foobar@example.org>

        # Set ui.tweakdefaults to ensure HGPLAIN is being set.
        tweakdefaults = True
    """)
    )

    (repo / "hello").write_text("utrecht")
    (repo / ".hgignore").touch()
    subprocess.check_call(["hg", "add", "--cwd", repo, "hello", ".hgignore"])
    subprocess.check_call(["hg", "commit", "--cwd", repo, "-m", "Bla1"])
    rev1 = (
        subprocess.run(
            ["hg", "log", "--cwd", repo, "-r", "tip", "--template", "{node}"],
            stdout=subprocess.PIPE,
            check=True,
        )
        .stdout.strip()
        .decode("utf8", errors="replace")
    )

    (repo / "hello").write_text("world")
    subprocess.check_call(["hg", "commit", "--cwd", repo, "-m", "Bla2"])
    rev2 = (
        subprocess.run(
            ["hg", "log", "--cwd", repo, "-r", "tip", "--template", "{node}"],
            stdout=subprocess.PIPE,
            check=True,
        )
        .stdout.strip()
        .decode("utf8", errors="replace")
    )

    subprocess.check_call(
        ["hg", "--cwd", repo, "bookmark", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"]
    )

    return HgRepo(repo, rev1, rev2)


@pytest.fixture
def hg(static_hg_repo: HgRepo, env: ManagedEnv) -> HgRepo:
    repo = env.dirs.home / "hg"
    shutil.copytree(static_hg_repo.path, repo)
    return dataclasses.replace(static_hg_repo, path=repo)


def test_fetch_unclean(hg: HgRepo, nix: Nix):
    (hg.path / "hello").write_text("unclean")
    result = (
        nix.nix(
            ["eval", "--impure", "--raw", "--expr", f"(builtins.fetchMercurial {hg.uri}).outPath"]
        )
        .run()
        .ok()
    )
    assert "is unclean" in result.stderr_plain
    assert (Path(result.stdout_plain) / "hello").read_text() == "unclean"


def test_fetch_current_rev(hg: HgRepo, nix: Nix):
    expr = f"(builtins.fetchMercurial {hg.uri}).outPath"
    result = nix.nix(["eval", "--impure", "--raw", "--expr", expr]).run().ok()
    assert (Path(result.stdout_plain) / "hello").read_text() == "world"


def test_pure_fetch_without_rev(hg: HgRepo, nix: Nix):
    expr = f'builtins.readFile (builtins.fetchMercurial {hg.uri} + "/hello")'
    result = nix.nix(["eval", "--raw", "--expr", expr]).run().expect(1)
    assert (
        "error: in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision"
        in result.stderr_s
    )


def test_pure_fetch_with_good_rev(hg: HgRepo, nix: Nix):
    expr = f'(builtins.fetchMercurial {{ url = {hg.uri}; rev = "{hg.rev2}"; }}).outPath'
    result = nix.nix(["eval", "--raw", "--expr", expr]).run().ok()
    assert (Path(result.stdout_plain) / "hello").read_text() == "world"


def test_pure_fetch_with_bad_rev(hg: HgRepo, nix: Nix):
    expr = f'(builtins.fetchMercurial {{ url = {hg.uri}; rev = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"; }}).outPath'
    result = nix.nix(["eval", "--raw", "--expr", expr]).run().expect(1)
    assert "hg failed with exit code" in result.stderr_s


def test_fetch_attrs(hg: HgRepo, nix: Nix):
    expr = f'builtins.removeAttrs (builtins.fetchMercurial {{ url = {hg.uri}; rev = "{hg.rev2}"; }}) [ "outPath" ]'
    result = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
    assert result["branch"] == "default"
    assert result["revCount"] == 2
    assert result["rev"] == hg.rev2


def test_caching(hg: HgRepo, nix: Nix):
    expr = f"(builtins.fetchMercurial {hg.uri}).outPath"
    path1 = nix.nix(["eval", "--impure", "--raw", "--expr", expr]).run().ok().stdout_plain
    shutil.rmtree(hg.path)
    path2 = nix.nix(["eval", "--impure", "--raw", "--expr", expr]).run().ok().stdout_plain
    assert path1 == path2
    assert (Path(path2) / "hello").read_text() == "world"

    # with TTL 0 refetch should fail
    result = nix.nix(["eval", "--impure", "--refresh", "--expr", expr]).run().expect(1)
    assert "error: 'hg pull' failed" in result.stderr_s


class TestExplicitHashes:
    def test_rev2(self, hg: HgRepo, nix: Nix):
        expr = f'(builtins.fetchMercurial {{ url = {hg.uri}; rev = "{hg.rev2}"; }}).outPath'
        path1 = nix.nix(["eval", "--raw", "--expr", expr]).run().ok().stdout_plain
        shutil.rmtree(hg.path)
        path2 = nix.nix(["eval", "--refresh", "--raw", "--expr", expr]).run().ok().stdout_plain
        assert path1 == path2

    def test_rev1(self, hg: HgRepo, nix: Nix):
        expr = f'(builtins.fetchMercurial {{ url = {hg.uri}; rev = "{hg.rev1}"; }}).outPath'
        path2 = nix.nix(["eval", "--refresh", "--raw", "--expr", expr]).run().ok().stdout_plain
        assert (Path(path2) / "hello").read_text() == "utrecht"


class TestFetchDirt:
    @pytest.fixture(scope="class")
    def static_changed_repo(
        self, static_hg_repo: HgRepo, tmp_path_factory: pytest.TempPathFactory
    ) -> HgRepo:
        repo = tmp_path_factory.mktemp("hg-changed")
        shutil.copytree(static_hg_repo.path, repo, dirs_exist_ok=True)

        for name in ["dir1", "dir2"]:
            (repo / name).mkdir()
        (repo / "dir1/foo").write_text("foo")
        (repo / "bar").write_text("bar")
        (repo / "dir2/bar").write_text("bar")
        subprocess.check_call(["hg", "add", "--cwd", repo, "dir1/foo"])
        subprocess.check_call(["hg", "rm", "--cwd", repo, "hello"])

        return dataclasses.replace(static_hg_repo, path=repo)

    # this intentionally overrides the module-level fixture name
    @pytest.fixture
    def hg(self, static_changed_repo: HgRepo, env: ManagedEnv) -> HgRepo:
        repo = env.dirs.home / "hg"
        shutil.copytree(static_changed_repo.path, repo)
        return dataclasses.replace(static_changed_repo, path=repo)

    def test_fetch_simple(self, hg: HgRepo, nix: Nix, command: Callable[..., Command]):
        expr = f"""
            let f = builtins.fetchMercurial {hg.uri};
            in {{ path = f.outPath; rev = f.rev; }}
        """
        result = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
        path = Path(result["path"])
        assert not (path / "hello").exists()
        assert not (path / "bar").exists()
        assert not (path / "dir2/bar").exists()
        assert (path / "dir1/foo").read_text() == "foo"
        assert result["rev"] == "0000000000000000000000000000000000000000"

        command(["hg", "commit", "--cwd", hg.path, "-m", "Bla3"]).run().ok()
        result2 = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
        assert result["path"] == result2["path"]

    def test_fetch_rev(self, hg: HgRepo, nix: Nix):
        expr = f"""
            let f = builtins.fetchMercurial {{ url = {hg.uri}; rev = "default"; }};
            in {{ path = f.outPath; rev = f.rev; }}
        """
        result = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
        path = Path(result["path"])
        assert (path / "hello").exists()
        assert not (path / "bar").exists()
        assert not (path / "dir2/bar").exists()
        assert not (path / "dir1/foo").exists()
        assert result["rev"] == hg.rev2


def test_fetch_with_name(hg: HgRepo, nix: Nix):
    """Passing a `name` argument should be reflected in the output path"""
    (hg.path / "hello").write_text("paris")
    expr = f'(builtins.fetchMercurial {{ url = {hg.uri}; name = "foo"; }}).outPath'
    path = (
        nix.nix(["eval", "--impure", "--refresh", "--raw", "--expr", expr]).run().ok().stdout_plain
    )
    assert path.endswith("-foo")


class TestNondefaultBranch:
    @pytest.fixture(autouse=True)
    def setup(self, hg: HgRepo, command: Callable[..., Command]):
        (hg.path / "hello").write_text("paris")
        command(["hg", "branch", "--cwd", hg.path, "meow"]).run().ok()
        command(["hg", "commit", "--cwd", hg.path, "-m", "Bla3"]).run().ok()

    def test_simple(self, hg: HgRepo, nix: Nix):
        expr = f'builtins.removeAttrs (builtins.fetchMercurial {hg.uri}) [ "outPath" ]'
        result = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
        assert result["branch"] == "meow"
        assert result["revCount"] == 3

    def test_older_rev(self, hg: HgRepo, nix: Nix, command: Callable[..., Command]):
        command(["hg", "update", "--cwd", hg.path, "1"]).run().ok()
        expr = f'builtins.removeAttrs (builtins.fetchMercurial {hg.uri}) [ "outPath" ]'
        result = nix.nix(["eval", "--impure", "--json", "--expr", expr]).run().json()
        assert result["branch"] == "default"
        assert result["revCount"] == 2
