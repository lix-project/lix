from testlib.fixtures.file_helper import merge_file_declaration
from typing import NamedTuple
import shutil
from testlib.fixtures.file_helper import File
from testlib.utils import get_global_asset_pack
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from pathlib import Path
from testlib.fixtures.env import ManagedEnv
import pytest
from collections.abc import Callable
from testlib.fixtures.command import Command

pytestmark = pytest.mark.no_daemon

eval_args = ["eval", "--impure", "--raw", "--expr"]

repo_files = {"repo": get_global_asset_pack(".git") | {".gitignore": File("")}}


@pytest.fixture
def repo(files: Path) -> Path:
    return files / "repo"


@pytest.fixture
def git(repo: Path, env: ManagedEnv) -> Callable[[list[str]], Command]:
    env.path.add_program("git")

    def wrapper(args: list[str]) -> Command:
        return Command(["git", *args], env, cwd=repo)

    return wrapper


class RepoRevs(NamedTuple):
    rev1: str
    rev2: str
    devrev: str


@pytest.fixture(autouse=True)
def repo_revs(git: Callable[[list[str]], Command], repo: Path, nix: Nix) -> RepoRevs:
    files = repo
    nix.settings.add_xp_feature("nix-command", "flakes")

    hello = files / "hello"
    hello.write_text("utrecht")
    git(["add", "hello", ".gitignore"]).run().ok()
    git(["commit", "-m", "Bla1"]).run().ok()
    git(["tag", "-a", "tag1", "-m", "tag1"]).run().ok()
    rev1 = git(["rev-parse", "HEAD"]).run().ok().stdout_plain

    hello.write_text("world")
    git(["commit", "-m", "Bla2", "-a"]).run().ok()
    git(["worktree", "add", files / "worktree"]).run().ok()
    git(["tag", "-a", "tag2", "-m", "tag2"]).run().ok()
    git(["checkout", "main"]).run().ok()
    rev2 = git(["rev-parse", "HEAD"]).run().ok().stdout_plain

    hello2 = files / "worktree" / "hello"
    hello2.write_text("hello")

    git(["checkout", "-b", "devtest"]).run().ok()
    (repo / "differentbranch").write_text("different file")
    git(["add", "differentbranch"]).run().ok()
    git(["commit", "-m", "Test2"]).run().ok()
    git(["checkout", "main"]).run().ok()
    devrev = git(["rev-parse", "devtest"]).run().ok().stdout_plain

    return RepoRevs(rev1, rev2, devrev)


@pytest.fixture
def devrev(repo_revs: RepoRevs) -> str:
    return repo_revs.devrev


@with_files(repo_files)
def test_fetch_worktree(nix: Nix, repo: Path):
    path_0 = (
        nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}/worktree").outPath'])
        .run()
        .ok()
        .stdout_plain
    )
    path_1 = (
        nix.nix(
            [
                *eval_args,
                f'(builtins.fetchTree {{ type = "git"; url = "file://{repo}/worktree";}}).outPath',
            ]
        )
        .run()
        .ok()
        .stdout_plain
    )

    assert path_0 == path_1
    assert (Path(path_0) / "hello").read_text() == "hello"


@with_files(repo_files)
def test_fetch_default_branch(nix: Nix, repo: Path):
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()

    assert (Path(res.stdout_plain) / "hello").read_text() == "world"


@with_files(repo_files)
def test_fetch_rev_another_branch(nix: Nix, repo: Path, devrev: str):
    # if this is not set, then the revision will be found
    nix.env["_NIX_FORCE_HTTP"] = "1"

    res = (
        nix.nix([*eval_args, f'builtins.fetchGit {{ url = "file://{repo}"; rev = "{devrev}";}}'])
        .run()
        .expect(1)
    )
    assert "Cannot find Git revision" in res.stderr_plain


@with_files(repo_files)
@pytest.mark.parametrize("parent_cwd", [False, True])
def test_allow_revs_as_refs(nix: Nix, repo: Path, devrev: str, parent_cwd: bool):
    # for 2.3 compat
    nix.env["_NIX_FORCE_HTTP"] = "1"
    res = (
        nix.nix(
            [
                *eval_args,
                f'builtins.readFile (builtins.fetchGit {{ url = "file://{repo}"; rev = "{devrev}"; allRefs = true;}} + "/differentbranch")',
            ],
            cwd=repo.parent.parent if parent_cwd else None,
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "different file"


@with_files(repo_files)
def test_allow_revs_as_refs_invalid(nix: Nix, repo: Path, devrev: str):
    # for 2.3 compat
    nix.env["_NIX_FORCE_HTTP"] = "1"
    res = (
        nix.nix(
            [
                *eval_args,
                f'builtins.readFile (builtins.fetchGit {{ url = "file://{repo}"; rev = "{devrev}"; ref = "lolkek";}} + "/differentbranch")',
            ],
            cwd=repo.parent.parent,
        )
        .run()
        .expect(1)
    )
    assert "Cannot find Git revision" in res.stderr_plain


@with_files(repo_files)
def test_pure_eval_no_rev(nix: Nix, repo: str):
    expr = f'builtins.readFile (fetchGit "file://{repo}" + "/hello")'
    res = nix.nix([*eval_args, expr]).run().ok()
    assert res.stdout_plain == "world"

    res = nix.nix(["eval", "--raw", "--expr", expr]).run().expect(1)
    assert "in pure evaluation mode, 'fetchTree' requires a locked input" in res.stderr_plain


@with_files(repo_files)
def test_pure_eval_with_hash(nix: Nix, repo: str, repo_revs: RepoRevs):
    nix.env["_NIX_FORCE_HTTP"] = "1"
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    path0 = res.stdout_plain

    res = (
        nix.nix(
            [
                "eval",
                "--raw",
                "--expr",
                f'(builtins.fetchGit {{ url = "file://{repo}"; rev = "{repo_revs.rev2}";}}).outPath',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == path0

    # Check that things are cached
    shutil.rmtree(repo)
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    assert res.stdout_plain == path0
    res = (
        nix.nix(["eval", "--impure", "--expr", f'(builtins.fetchGit "file://{repo}").revCount'])
        .run()
        .ok()
    )
    assert res.stdout_plain == "2"

    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").rev']).run().ok()
    assert res.stdout_plain == repo_revs.rev2

    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").shortRev']).run().ok()
    assert res.stdout_plain == repo_revs.rev2[0:7]


@with_files(repo_files)
def test_pure_eval_with_rev(nix: Nix, repo: str, repo_revs: RepoRevs):
    res = (
        nix.nix(
            [
                "eval",
                "--raw",
                "--expr",
                f'builtins.readFile (fetchGit {{ url = "file://{repo}"; rev = "{repo_revs.rev2}";}} + "/hello")',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "world"


@with_files(repo_files)
def test_pure_eval_refresh_explicit_hash(nix: Nix, repo: str, repo_revs: RepoRevs):
    nix.env["_NIX_FORCE_HTTP"] = "1"
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    path0 = res.stdout_plain
    shutil.rmtree(repo)

    res = (
        nix.nix(
            [
                "eval",
                "--refresh",
                "--raw",
                "--expr",
                f'(builtins.fetchGit {{url = "file://{repo}"; rev = "{repo_revs.rev2}";}}).outPath',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == path0

    res = (
        nix.nix(
            [
                "eval",
                "--refresh",
                "--raw",
                "--expr",
                f'(builtins.fetchGit {{url = "file://{repo}"; rev = "{repo_revs.rev1}";}}).outPath',
            ]
        )
        .run()
        .ok()
    )
    assert (Path(res.stdout_plain) / "hello").read_text() == "utrecht"


@with_files(repo_files)
def test_clean_worktree(nix: Nix, repo: str):
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    path0 = res.stdout_plain
    res = nix.nix([*eval_args, f"(builtins.fetchGit {repo}).outPath"]).run().ok()
    assert res.stdout_plain == path0


@with_files(
    merge_file_declaration(
        repo_files,
        {"repo": {"dir1": {"foo": File("foo")}, "dir2": {"bar": File("bar")}, "bar": File("bar")}},
    )
)
def test_unclean_worktree_tracked_no_uncomitted(
    nix: Nix, repo: repo, git: Callable[[list[str]], Command], repo_revs: RepoRevs
):
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    path0 = res.stdout_plain

    git(["add", "dir1/foo"]).run().ok()
    git(["rm", "hello"]).run().ok()

    res = nix.nix([*eval_args, f"(builtins.fetchGit {repo}).outPath"]).run().ok()
    path2 = Path(res.stdout_plain)
    for p in ["hello", "bar", "dir2/bar", ".git"]:
        assert not (path2 / p).exists()
    foo_path = path2 / "dir1" / "foo"
    assert foo_path.exists()
    assert foo_path.read_text() == "foo"

    for attr, exp in [
        ("rev", "0000000000000000000000000000000000000000"),
        ("dirtyRev", f"{repo_revs.rev2}-dirty"),
        ("dirtyShortRev", f"{repo_revs.rev2[0:7]}-dirty"),
    ]:
        res = nix.nix([*eval_args, f"(builtins.fetchGit {repo}).{attr}"]).run().ok()
        assert res.stdout_plain == exp

    # ... unless we're using an explicit ref or rev.
    res = (
        nix.nix([*eval_args, f'(builtins.fetchGit {{ url = {repo}; ref = "main";}}).outPath'])
        .run()
        .ok()
    )
    assert res.stdout_plain == path0
    res = (
        nix.nix(
            [
                "eval",
                "--raw",
                "--expr",
                f'(builtins.fetchGit {{ url = {repo}; rev = "{repo_revs.rev2}";}}).outPath',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == path0

    # Committing should not affect the store path.
    git(["commit", "-m", "Bla3", "-a"]).run().ok()
    res = (
        nix.nix(
            [
                "eval",
                "--impure",
                "--refresh",
                "--raw",
                "--expr",
                f'(builtins.fetchGit "file://{repo}").outPath',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == str(path2)

    for attr, exists in [("rev", "true"), ("dirtyRev", "false"), ("dirtyShortRev", "false")]:
        res = (
            nix.nix(
                [
                    "eval",
                    "--impure",
                    "--expr",
                    f'builtins.hasAttr "{attr}" (builtins.fetchGit {repo})',
                ]
            )
            .run()
            .ok()
        )
        assert res.stdout_plain == exists


@with_files(repo_files)
def test_bad_hash(nix: Nix, repo_revs: RepoRevs, repo: str):
    res = nix.nix([*eval_args, f'(builtins.fetchGit "file://{repo}").outPath']).run().ok()
    path0 = res.stdout_plain
    res = (
        nix.nix(
            [
                *eval_args,
                f'(builtins.fetchGit {{url = {repo}; rev = "{repo_revs.rev2}"; narHash = "sha256-B5yIPHhEm0eysJKEsO7nqxprh9vcblFxpJG11gXJus1=";}}).outPath',
            ]
        )
        .run()
        .expect(102)
    )
    assert "NAR hash mismatch in input" in res.stderr_plain

    res = (
        nix.nix(
            [
                *eval_args,
                f'(builtins.fetchGit {{ url = {repo}; rev = "{repo_revs.rev2}"; narHash = "sha256-oFveoYbIlm/9CEfnB99fYpKejPnFOLwv01zKJRi+vy0=";}}).outPath',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == path0
