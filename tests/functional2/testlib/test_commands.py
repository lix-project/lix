import logging
import os
import stat
from pathlib import Path
from subprocess import CalledProcessError

import pytest
from _pytest.logging import LogCaptureFixture
from functional2.testlib.commands import Command
from functional2.testlib.fixtures.file_helper import File


def test_command_valid_runs():
    cmd = Command(["echo", "water"]).with_env(**os.environ.copy())
    cmd.run().ok()


def test_command_captures_stdout():
    cmd = Command(["echo", "fire"]).with_env(**os.environ.copy())
    res = cmd.run().ok()
    assert res.stdout_s == "fire\n"


def test_command_plain_strips():
    cmd = Command(["echo", "   earth   "]).with_env(**os.environ.copy())
    res = cmd.run().ok()
    assert res.stdout_plain == "earth"


def test_command_stdin_passed_correctly():
    inp = b"air"
    cmd = Command(["cat", "/dev/stdin"]).with_stdin(inp).with_env(**os.environ.copy())
    res = cmd.run().ok()
    assert res.stdout == inp


def test_command_expect_failure():
    cmd = Command(["grep", "xxx"]).with_env(**os.environ.copy()).with_stdin(b"")
    cmd.run().expect(1)


@pytest.mark.parametrize(
    "files",
    [
        {
            "script.sh": File(
                "#!/bin/sh\necho forb\nexit 1", mode=stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO
            )
        }
    ],
    indirect=True,
)
def test_command_ok_fails_on_bad_exit_code(files: Path, caplog: LogCaptureFixture):
    cmd = Command(["./script.sh"], cwd=files).with_env(**os.environ.copy())
    res = cmd.run()

    with pytest.raises(CalledProcessError), caplog.at_level(logging.ERROR):
        res.ok()
    msgs = caplog.messages
    assert len(msgs) == 2
    out_msg, err_msg = msgs
    assert out_msg == "stdout: forb\n"
    assert err_msg == "stderr: "


@pytest.mark.parametrize(
    "files",
    [
        {
            "script.sh": File(
                "#!/bin/sh\necho drgn fops\nexit 2", mode=stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO
            )
        }
    ],
    indirect=True,
)
def test_command_exec_fails_on_other_bad_exit_code(files: Path, caplog: LogCaptureFixture):
    cmd = Command(["./script.sh"], cwd=files).with_env(**os.environ.copy())
    res = cmd.run()

    with pytest.raises(CalledProcessError), caplog.at_level(logging.ERROR):
        res.expect(1)
    msgs = caplog.messages
    assert len(msgs) == 2
    out_msg, err_msg = msgs
    assert out_msg == "stdout: drgn fops\n"
    assert err_msg == "stderr: "
