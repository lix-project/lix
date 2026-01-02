import contextlib
import dataclasses
import json
import logging
import subprocess
from pathlib import Path
from collections.abc import Callable
from typing import Any

import pytest

from testlib.fixtures.env import ManagedEnv
from testlib.terminal_code_eater import eat_terminal_codes

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


class RunningCommand(contextlib.AbstractContextManager):
    argv: list[str]
    stdin: bytes | None = None
    _proc: subprocess.Popen

    def __init__(self, argv: list[str], stdin: bytes | None, proc: subprocess.Popen):
        self.argv = argv
        self.stdin = stdin
        self._proc = proc

    def __exit__(self, exc_type, exc, tb):  # noqa: ANN001
        self.kill()

    def kill(self) -> CommandResult | None:
        """
        Kill the process immediately without waiting for it to exit cleanly.
        :return: `None` if the process was already dead, else the process result.
        """
        if self._proc is not None:
            self._proc.kill()
            # wait forever. killing must never fail, so we'd rather timeout than not notice errors here
            return self.wait()
        return None

    def terminate(self, timeout: float | None = None) -> CommandResult | None:
        """
        Send a termination signal to the process and waits for it to exit. `None` timeouts are treated as infinite.
        :return: `None` if timeout expired before the process exited, else the process result.
        """
        self._proc.terminate()
        return self.wait(timeout)

    def wait(self, timeout: float | None = None) -> CommandResult | None:
        """
        Waits for the process to exit. `None` timeouts are treated as infinite.
        :return: `None` if timeout expired before the process exited, else the process result.
        """
        try:
            stdout, stderr = self._proc.communicate(input=self.stdin, timeout=timeout)
            rc = self._proc.returncode
            self._proc = None
            return CommandResult(cmd=self.argv, rc=rc, stdout=stdout, stderr=stderr)
        except subprocess.TimeoutExpired:
            return None


@dataclasses.dataclass
class Command:
    argv: list[str]
    _env: ManagedEnv
    exe: Path | None = None
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
        return self.start().wait()

    def start(self) -> RunningCommand:
        """
        Starts the configured command
        :return: Handle to the running command for interaction or waiting
        """
        self._logger.debug("Running Command with args: %s", self.argv)
        proc = subprocess.Popen(
            self.argv,
            executable=self.exe,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE if self.stdin else subprocess.DEVNULL,
            cwd=self.cwd,
            env=self._env.to_env(),
        )
        return RunningCommand(self.argv, self.stdin, proc)


@pytest.fixture
def command(env: ManagedEnv) -> Callable[..., Command]:
    def wrapper(*args, **kwargs) -> Command:
        return Command(_env=env, *args, **kwargs)

    return wrapper
