import json
import pytest
from testlib.fixtures.nix import Nix, NixDaemon, NixDaemonProtocol

pytestmark = pytest.mark.no_daemon


@pytest.fixture(autouse=True)
def setup(nix: Nix):
    nix.settings.add_xp_feature("nix-command")


def test_ping(nix: Nix):
    info = nix.nix(["store", "ping"]).run().ok().stderr_plain
    assert "Store URL: local" in info


def test_ping_json(nix: Nix):
    info = nix.nix(["store", "ping", "--json"]).run().ok().json()
    assert info["url"] == "local"


def test_ping_daemon(nix: Nix, daemon: NixDaemon):
    version = nix.nix(["daemon", "--version"]).run().ok().stdout_plain
    version = version.splitlines()[0].split()[-1]
    with daemon(nix) as inner:
        info = nix.nix(["store", "ping"]).run().ok().stderr_plain
        assert f"Version: {version}" in info
        info = inner.nix(["store", "ping", "--json"]).run().ok().json()
        assert info["url"] == inner.settings.store
        # we transport some feature flags after the real version number for non-rpc connections
        suffix = (
            " (sshng-extended-options)"
            if inner.daemon_protocol
            in [NixDaemonProtocol.LEGACY, NixDaemonProtocol.LEGACY_COMBINED]
            else ""
        )
        assert info["version"] == f"{version}{suffix}"


def test_ping_bad_store(nix: Nix):
    nix.settings.store = f"unix:{nix.env.dirs.home}"
    error = nix.nix(["store", "ping"]).run().expect(1).stderr_plain
    assert "could not connect" in error


def test_ping_daemon_warning_severity(nix: Nix, daemon: NixDaemon):
    with daemon(nix) as inner:
        result = (
            inner.nix(
                [
                    "store",
                    "ping",
                    "--option",
                    "substituters",
                    "https://untrusted.invalid",
                    "--log-format",
                    "internal-json",
                ]
            )
            .run()
            .ok()
        )
    events = [
        json.loads(line[5:]) for line in result.stderr_s.splitlines() if line.startswith("@nix ")
    ]
    warnings = [
        event for event in events if "ignoring untrusted substituter" in event.get("msg", "")
    ]
    assert len(warnings) == 1
    assert warnings[0]["level"] == 1
