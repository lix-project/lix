import json
import re
import shutil
from pathlib import Path

import pytest

from testlib.fixtures.git import GitCmd
from testlib.fixtures.nix import Nix

pytestmark = pytest.mark.no_daemon

eval_args = ["eval", "--impure", "--raw", "--expr"]


def _init_repo(git_cmd: GitCmd, repo: Path, object_format: str, contents: str) -> None:
    git_cmd(None, "init", f"--object-format={object_format}", "--initial-branch=main", repo).ok()
    (repo / "hello").write_text(contents)
    git_cmd(repo, "config", "user.name", "functional2")
    git_cmd(repo, "config", "user.email", "noreply@noreply.com")
    git_cmd(repo, "add", "hello").ok()
    git_cmd(repo, "commit", "-m", "init").ok()


@pytest.fixture
def sha256_repo(files: Path, git_cmd: GitCmd) -> Path:
    repo = files / "repo-sha256"
    repo.mkdir()
    _init_repo(git_cmd, repo, "sha256", "sha256 world")
    return repo


@pytest.fixture
def sha256_bare(files: Path, sha256_repo: Path, git_cmd: GitCmd) -> Path:
    bare = files / "bare-sha256"
    git_cmd(None, "clone", "--bare", sha256_repo, bare).ok()
    return bare


def test_sha256_local_worktree(nix: Nix, sha256_repo: Path) -> None:
    # Non-bare local repository, it never touch the bare cache, so this exercises
    # the local `git rev-parse` parsing path.

    rev = (
        nix.nix([*eval_args, f'(builtins.fetchGit "file://{sha256_repo}").rev'], flake=True)
        .run()
        .ok()
    )
    assert re.fullmatch(r"[0-9a-f]{64}", rev.stdout_plain)

    out = (
        nix.nix([*eval_args, f'(builtins.fetchGit "file://{sha256_repo}").outPath'], flake=True)
        .run()
        .ok()
    )
    assert (Path(out.stdout_plain) / "hello").read_text() == "sha256 world"


def test_sha256_bare_forces_cache(nix: Nix, sha256_bare: Path, git_cmd: GitCmd) -> None:
    # A bare repository is treated as a remote URL in the code, so this will
    # exercise the Git cache and create an entry over there.
    rev = (
        nix.nix([*eval_args, f'(builtins.fetchGit "file://{sha256_bare}").rev'], flake=True)
        .run()
        .ok()
    )
    assert re.fullmatch(r"[0-9a-f]{64}", rev.stdout_plain)

    # The cache now holds a SHA-256 bare repository.
    assert nix.env.dirs.xdg_cache_home is not None
    cached = list((nix.env.dirs.xdg_cache_home / "nix" / "gitv3").iterdir())
    assert len(cached) == 1
    cached_format = git_cmd(cached[0], "rev-parse", "--show-object-format").ok().stdout_plain
    assert cached_format.strip() == "sha256"


def test_sha256_explicit_rev_roundtrip(nix: Nix, sha256_repo: Path, git_cmd: GitCmd) -> None:
    # The fetcher must resolve the revision Git itself reports…
    git_rev = git_cmd(sha256_repo, "rev-parse", "HEAD").ok().stdout_plain.strip()
    fetched_rev = (
        nix.nix([*eval_args, f'(builtins.fetchGit "file://{sha256_repo}").rev'], flake=True)
        .run()
        .ok()
        .stdout_plain
    )
    assert fetched_rev == git_rev

    # …and accept that revision back as an explicit `rev`.
    url = f"file://{sha256_repo}"
    out = (
        nix.nix(
            [*eval_args, f'(builtins.fetchGit {{ url = "{url}"; rev = "{git_rev}"; }}).outPath'],
            flake=True,
        )
        .run()
        .ok()
    )
    assert (Path(out.stdout_plain) / "hello").read_text() == "sha256 world"


def test_sha256_flake_lock_roundtrip(nix: Nix, files: Path, sha256_repo: Path) -> None:
    nix.settings.add_xp_feature("nix-command", "flakes")

    client = files / "client"
    client.mkdir()
    (client / "flake.nix").write_text(f"""{{
        inputs.dep = {{ url = "git+file://{sha256_repo}"; flake = false; }};
        outputs = inputs: {{ answer = builtins.readFile (inputs.dep.outPath + "/hello"); }};
    }}""")

    nix.nix(["flake", "lock", str(client)], flake=True).run().ok()
    lock = json.loads((client / "flake.lock").read_text())
    assert re.fullmatch(r"[0-9a-f]{64}", lock["nodes"]["dep"]["locked"]["rev"])
    assert (
        nix.nix(["eval", "--raw", f"{client}#answer"], flake=True).run().ok().stdout_plain
        == "sha256 world"
    )


def test_cache_recreated_on_object_format_change(nix: Nix, files: Path, git_cmd: GitCmd) -> None:
    bare = files / "bare"

    def make_bare(work_name: str, object_format: str, contents: str) -> None:
        work = files / work_name
        work.mkdir()
        _init_repo(git_cmd, work, object_format, contents)
        if bare.exists():
            shutil.rmtree(bare)
        git_cmd(None, "clone", "--bare", work, bare).ok()

    make_bare("work-sha1", "sha1", "sha1 world")
    url = f"file://{bare}"

    rev = nix.nix([*eval_args, f'(builtins.fetchGit "{url}").rev'], flake=True).run().ok()
    assert re.fullmatch(r"[0-9a-f]{40}", rev.stdout_plain)

    # Repoint the URL at a SHA-256 repository.
    make_bare("work-sha256", "sha256", "sha256 world")

    out = (
        nix.nix(
            [
                "eval",
                "--impure",
                "--raw",
                "--tarball-ttl",
                "0",
                "--expr",
                f'(builtins.fetchGit "{url}").outPath',
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    assert "recreating Git cache" in out.stderr_plain
    assert (Path(out.stdout_plain) / "hello").read_text() == "sha256 world"

    # The recreated cache is reused instead of being recreated a second time.
    again = (
        nix.nix(
            [
                "eval",
                "--impure",
                "--raw",
                "--tarball-ttl",
                "0",
                "--expr",
                f'(builtins.fetchGit "{url}").rev',
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    assert "recreating Git cache" not in again.stderr_plain
    assert re.fullmatch(r"[0-9a-f]{64}", again.stdout_plain)


def test_sha1_repo_with_sha256_client_default(nix: Nix, files: Path, git_cmd: GitCmd) -> None:
    repo = files / "repo-sha1"
    repo.mkdir()
    _init_repo(git_cmd, repo, "sha1", "sha1 world")

    bare = files / "bare-sha1"
    git_cmd(None, "clone", "--bare", repo, bare).ok()

    # Same as init.defaultObjectFormat=sha256 which is only available since Git ≥ 2.47.
    nix.env["GIT_DEFAULT_HASH"] = "sha256"

    rev = nix.nix([*eval_args, f'(builtins.fetchGit "file://{bare}").rev'], flake=True).run().ok()
    assert re.fullmatch(r"[0-9a-f]{40}", rev.stdout_plain)
