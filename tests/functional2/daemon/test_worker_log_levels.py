import json
from pathlib import Path
import shlex
import socket
import struct
import subprocess
import sys
from urllib.parse import quote, urlencode

import pytest

from testlib.fixtures.nix import Nix

pytestmark = pytest.mark.no_daemon

MAGIC_1 = 0x6E697863
MAGIC_2 = 0x6478696F
PROTOCOL = 0x123
LOG_LEVELS = 0x6C786C76
STDERR_NEXT = 0x6F6C6D67
STDERR_LOG = 0x6C6F676D
STDERR_LAST = 0x616C7473


def numbers(*values: int) -> bytes:
    return struct.pack("<" + "Q" * len(values), *values)


def string(value: str) -> bytes:
    data = value.encode()
    return numbers(len(data)) + data + b"\0" * (-len(data) % 8)


@pytest.mark.parametrize("typed", [False, True])
def test_ssh_startup_log_level(nix: Nix, tmp_path: Path, typed: bool):
    nix.settings.add_xp_feature("nix-command")
    nix.env.path.add_program("bash")
    message = "startup warning\n"
    packet = string("2.94.3") + numbers(1)
    packet += numbers(STDERR_LOG, 1) if typed else numbers(STDERR_NEXT)
    packet += string(message) + numbers(STDERR_LAST)
    peer = tmp_path / "peer.py"
    peer.write_text(
        "import struct, sys\n"
        "def read(): return struct.unpack('<Q', sys.stdin.buffer.read(8))[0]\n"
        f"assert read() == {MAGIC_1}\n"
        f"sys.stdout.buffer.write({numbers(MAGIC_2, PROTOCOL)!r})\n"
        "sys.stdout.buffer.flush()\n"
        f"assert [read(), read(), read()] == [{PROTOCOL}, 0, {LOG_LEVELS}]\n"
        f"sys.stdout.buffer.write({packet!r})\n"
        "sys.stdout.buffer.flush()\n"
    )
    # A peer without extended-options support must receive no SetOptions request.
    uri = "ssh-ng://localhost?" + urlencode(
        {"remote-program": shlex.join([sys.executable, str(peer)])}, quote_via=quote
    )
    result = nix.nix(["store", "ping", "--store", uri, "--log-format", "internal-json"]).run().ok()
    events = [
        json.loads(line[5:]) for line in result.stderr_s.splitlines() if line.startswith("@nix ")
    ]
    messages = [event for event in events if event.get("msg") == message.rstrip()]
    assert len(messages) == 1
    assert messages[0]["level"] == (1 if typed else 0)


@pytest.mark.parametrize("capability", [0, LOG_LEVELS])
def test_stdio_daemon_log_level(nix: Nix, capability: int):
    nix.settings.add_xp_feature("nix-command")
    nix.settings.to_env_overlay(nix.env)
    client, server = socket.socketpair()
    client.settimeout(10)
    process = subprocess.Popen(
        [str(nix.env.dirs.nix_bin_dir / "nix"), "daemon", "--stdio"],
        stdin=server,
        stdout=server,
        stderr=subprocess.PIPE,
        env=nix.env.to_env(),
    )
    server.close()
    stream = client.makefile("rb")

    def read_number() -> int:
        return struct.unpack("<Q", stream.read(8))[0]

    def read_string() -> bytes:
        size = read_number()
        return stream.read((size + 7) // 8 * 8)[:size]

    try:
        client.sendall(numbers(MAGIC_1))
        assert (read_number(), read_number()) == (MAGIC_2, PROTOCOL)
        client.sendall(numbers(PROTOCOL, 0, capability))
        read_string()  # daemon version
        assert read_number() == 1  # trusted stdio client
        assert read_number() == STDERR_LAST
        # This setting is explicitly ignored with a warning; no plugin is loaded.
        client.sendall(
            numbers(19, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1)
            + string("plugin-files")
            + string("/not-a-plugin")
        )
        assert read_number() == (STDERR_LOG if capability else STDERR_NEXT)
        if capability:
            assert read_number() == 1
        assert b"Ignoring the client-specified plugin-files" in read_string()
        assert read_number() == STDERR_LAST
    finally:
        stream.close()
        client.close()
        try:
            process.communicate(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
