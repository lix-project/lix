import dataclasses
import json
import logging
import subprocess
from pathlib import Path
from collections.abc import Callable
from typing import Any

import pytest

from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.terminal_code_eater import eat_terminal_codes


logger = logging.getLogger(__name__)


@dataclasses.dataclass
class CommandResult:
    cmd: list[str]
    """Command arguments which were run"""
    rc: int
    """Return code"""
    stderr: bytes
    """Outputted stderr"""
    stdout: bytes
    """Outputted stdout"""

    def ok(self) -> "CommandResult":
        """
        assumes a return code of 0
        :raises CalledProcessError: if the return code wasn't 0 and logs the processes stdout and stderr
        """
        __tracebackhide__ = True
        return self.expect(0)

    def expect(self, rc: int) -> "CommandResult":
        """
        assumes a return code of `rc`
        :param rc: The expected return code
        :raises CalledProcessError: if the return code wasn't `rc` and logs the processes stdout and stderr
        """
        __tracebackhide__ = True
        if self.rc != rc:
            logger.error("stdout: %s", self.stdout_s)
            logger.error("stderr: %s", self.stderr_s)
            exc = subprocess.CalledProcessError(
                returncode=self.rc, cmd=self.cmd, stderr=self.stderr, output=self.stdout
            )
            raise exc
        return self

    @property
    def stdout_s(self) -> str:
        """Command stdout as str"""
        return self.stdout.decode("utf-8", errors="replace")

    @property
    def stderr_s(self) -> str:
        """Command stderr as str"""
        return self.stderr.decode("utf-8", errors="replace")

    @property
    def stdout_plain(self) -> str:
        """Command stderr as str with terminal escape sequences eaten and whitespace stripped"""
        return eat_terminal_codes(self.stdout).decode("utf-8", errors="replace").strip()

    @property
    def stderr_plain(self) -> str:
        """Command stderr as str with terminal escape sequences eaten and whitespace stripped"""
        return eat_terminal_codes(self.stderr).decode("utf-8", errors="replace").strip()

    def json(self) -> Any:
        """
        Assumes an ok() result and returns the Commands stdout parsed as json
        :return: A parsed json object
        """
        __tracebackhide__ = True
        self.ok()
        return json.loads(self.stdout)


@dataclasses.dataclass
class Command:
    argv: list[str]
    _env: ManagedEnv
    stdin: bytes | None = None
    cwd: Path = dataclasses.field(default=None)
    _logger: logging.Logger = dataclasses.field(default=logger, init=False)

    def __post_init__(self):
        if self.cwd is None:
            self.cwd = self._env.dirs.home

    def with_stdin(self, stdin: bytes) -> "Command":
        self.stdin = stdin
        return self

    def set_args(self, *argv: str) -> "Command":
        self.argv = list(argv)
        return self

    def run(self) -> CommandResult:
        """
        Runs the configured command
        :return: Information about the Result of the execution
        """
        self._logger.debug("Running Command with args: %s", self.argv)
        proc = subprocess.Popen(
            self.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE if self.stdin else subprocess.DEVNULL,
            cwd=self.cwd,
            env=self._env.to_env(),
        )
        (stdout, stderr) = proc.communicate(input=self.stdin)
        rc = proc.returncode
        return CommandResult(cmd=self.argv, rc=rc, stdout=stdout, stderr=stderr)


@pytest.fixture
def command(env: ManagedEnv) -> Callable[..., Command]:
    def wrapper(*args, **kwargs) -> Command:
        return Command(_env=env, *args, **kwargs)

    return wrapper
