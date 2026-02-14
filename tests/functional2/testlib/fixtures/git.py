from pathlib import Path
from collections.abc import Callable

import pytest

from testlib.fixtures.command import CommandResult, Command, RunningCommand
from testlib.fixtures.env import ManagedEnv


type GitCmd = Callable[[Path | None, *tuple[str | Path, ...]], RunningCommand]
type Git = Callable[[Path | None, *tuple[str | Path, ...]], CommandResult]


@pytest.fixture
def git_cmd(env: ManagedEnv) -> GitCmd:
    """
    Provides a convenient wrapper to call git. Adds `git` to PATH, sets author and commit
    date environment variables to fixed values. If `path` is not None it will be added to
    the git invocation a `-C` argument. The remaining arguments are forwarded to Command,
    then `run` is called on the resulting Command object. The RunningCommand is returned.
    """

    env.path.add_program("git")
    # ensure that commit hashes are as deterministic as possible
    env["GIT_AUTHOR_DATE"] = "@42 +0000"
    env["GIT_COMMITTER_DATE"] = "@23 +0000"

    def call(path: Path | None, *args: str | Path, **kwargs: str | Path) -> RunningCommand:
        return Command(
            ["git", *(["-C", path] if path is not None else []), *args], _env=env, **kwargs
        ).run()

    return call


@pytest.fixture
def git(git_cmd: GitCmd) -> Git:
    """
    Provides a convenient wrapper to call git. Same as `git_cmd`, but also calls `ok()`.
    """

    def call(path: Path | None, *args: str | Path, **kwargs: str | Path) -> CommandResult:
        return git_cmd(path, *args, **kwargs).ok()

    return call
