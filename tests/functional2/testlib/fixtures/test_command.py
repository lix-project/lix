import logging
import stat
from collections.abc import Callable
from subprocess import CalledProcessError

import pytest
from _pytest.logging import LogCaptureFixture
from functional2.testlib.fixtures.command import Command
from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.fixtures.file_helper import File, with_files


def test_command_valid_runs(command: Callable[[list[str]], Command]):
    cmd = command(["echo", "water"])
    cmd.run().ok()


def test_command_captures_stdout(command: Callable[[list[str]], Command]):
    cmd = command(["echo", "fire"])
    res = cmd.run().ok()
    assert res.stdout_s == "fire\n"


def test_command_plain_strips(command: Callable[[list[str]], Command]):
    cmd = command(["echo", "   earth   "])
    res = cmd.run().ok()
    assert res.stdout_plain == "earth"


def test_command_stdin_passed_correctly(command: Callable[[list[str]], Command]):
    inp = b"air"
    cmd = command(["cat", "/dev/stdin"]).with_stdin(inp)
    res = cmd.run().ok()
    assert res.stdout == inp


def test_command_expect_failure(env: ManagedEnv):
    env.path.add_program("grep")
    Command(_env=env, argv=["grep", "xxx"], stdin=b"").run().expect(1)


@with_files(
    {
        "script.sh": File(
            "#!/bin/sh\necho forb\nexit 1", mode=stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO
        )
    }
)
def test_command_ok_fails_on_bad_exit_code(
    command: Callable[[list[str]], Command], caplog: LogCaptureFixture
):
    cmd = command(["./script.sh"])

    with pytest.raises(CalledProcessError), caplog.at_level(logging.ERROR):
        cmd.run().ok()
    msgs = caplog.messages
    assert len(msgs) == 2
    out_msg, err_msg = msgs
    assert out_msg == "stdout: forb\n"
    assert err_msg == "stderr: "


@with_files(
    {
        "script.sh": File(
            "#!/bin/sh\necho drgn fops\nexit 2", mode=stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO
        )
    }
)
def test_command_exec_fails_on_other_bad_exit_code(
    command: Callable[[list[str]], Command], caplog: LogCaptureFixture
):
    cmd = command(["./script.sh"])

    with pytest.raises(CalledProcessError), caplog.at_level(logging.ERROR):
        cmd.run().expect(1)
    msgs = caplog.messages
    assert len(msgs) == 2
    out_msg, err_msg = msgs
    assert out_msg == "stdout: drgn fops\n"
    assert err_msg == "stderr: "
