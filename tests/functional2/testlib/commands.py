import dataclasses
import json
import logging
import subprocess
from pathlib import Path
from typing import Any

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
        return self.expect(0)

    def expect(self, rc: int) -> "CommandResult":
        """
        assumes a return code of `rc`
        :param rc: The expected return code
        :raises CalledProcessError: if the return code wasn't `rc` and logs the processes stdout and stderr
        """
        if self.rc != rc:
            logger.error("stdout: %s", self.stdout_s)
            logger.error("stderr: %s", self.stderr_s)
            raise subprocess.CalledProcessError(
                returncode=self.rc, cmd=self.cmd, stderr=self.stderr, output=self.stdout
            )
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
        self.ok()
        return json.loads(self.stdout)


@dataclasses.dataclass
class Command:
    """
    Provides a way of configuring a shell command and then running it
    calls Popen internally
    """

    argv: list[str]
    """
    Arguments of the Process; argv[0] is the name of the binary
    """
    env: dict[str, str] = dataclasses.field(default_factory=dict)
    """
    environment variables; Note that `$PATH` is not added by default.
    Use `.with_env(**os.environ.copy())` to add `$PATH`
    """
    stdin: bytes | None = None
    """
    Things to pipe into stdin of the process
    """
    cwd: Path | None = None
    """
    current-working-directory fo the process
    """

    def with_env(self, **kwargs) -> "Command":
        """
        sets the env to the given environment variables
        :param kwargs: keyword arguments containing the environment
        :return: self, command is chainable
        """
        self.env = kwargs
        return self

    def update_env(self, **kwargs) -> "Command":
        """
        updates the current environment with the given dict of variables
        :param kwargs: new or updated environment variables
        :return: self, command is chainable
        """
        self.env.update(kwargs)
        return self

    def with_stdin(self, stdin: bytes) -> "Command":
        """
        sets the input provided to stdin of the commands
        :param stdin: data to pipe into stdin
        :return: self, command is chainable
        """
        self.stdin = stdin
        return self

    def run(self) -> CommandResult:
        """
        Runs the configured command
        :return: Information about the Result of the execution
        """
        proc = subprocess.Popen(
            self.argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE if self.stdin else subprocess.DEVNULL,
            cwd=self.cwd,
            env=self.env,
        )
        (stdout, stderr) = proc.communicate(input=self.stdin)
        rc = proc.returncode
        return CommandResult(cmd=self.argv, rc=rc, stdout=stdout, stderr=stderr)
