import pytest

from testlib.fixtures.command import CommandResult
from testlib.fixtures.file_helper import CopyFile, with_files
from testlib.fixtures.nix import Nix, NixDaemon, NixDaemonProtocol
from testlib.utils import get_global_asset

EXTENDED_OPTIONS_KEY = (
    "daemon-advertised-extended-options-support-but-does-not-actually-implement-it"
)


def _assert_not_restricted(result: CommandResult, name: str):
    assert f"Ignoring the client-specified setting '{name}'" not in result.stderr_plain
    assert f"Ignoring the client-specified preference '{name}'" not in result.stderr_plain


def _assert_tail(result: CommandResult, n: int):
    assert f"last {n} log lines" in result.stderr_plain
    assert "line-30" in result.stderr_plain
    if n < 30:
        assert f"line-{30 - n + 1}" in result.stderr_plain
        assert f"line-{30 - n}" not in result.stderr_plain
    _assert_not_restricted(result, "log-lines")
    assert EXTENDED_OPTIONS_KEY not in result.stderr_plain


@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_restricted_setting_still_warned(nix: Nix, daemon: NixDaemon):
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--no-require-sigs"]).run()
        result.expect(1)
        assert "Ignoring the client-specified setting 'require-sigs'" in result.stderr_plain


@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_file_transfer_preference_not_warned(nix: Nix, daemon: NixDaemon):
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--max-connect-timeout", "10"]).run()
        result.expect(1)
        _assert_not_restricted(result, "max-connect-timeout")


@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_client_only_setting_not_forwarded(nix: Nix, daemon: NixDaemon):
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--tarball-ttl", "1"]).run()
        result.expect(1)
        assert "tarball-ttl" not in result.stderr_plain
        _assert_not_restricted(result, "tarball-ttl")


@pytest.mark.parametrize("daemon", [NixDaemonProtocol.LIX_XP_1], indirect=True)
@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_log_lines_untrusted(nix: Nix, daemon: NixDaemon):
    nix.settings["log-format"] = "raw"
    nix.settings.add_xp_feature("nix-command")
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--log-lines", "5"]).run()
        result.expect(1)
        _assert_tail(result, 5)


@pytest.mark.parametrize("daemon", [NixDaemonProtocol.LIX_XP_1], indirect=True)
@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_log_lines_zero(nix: Nix, daemon: NixDaemon):
    nix.settings["log-format"] = "raw"
    nix.settings.add_xp_feature("nix-command")
    with daemon(nix) as inner:
        control = inner.nix_build(["./fail.nix"]).run()
        control.expect(1)
        _assert_tail(control, 25)

        result = inner.nix_build(["./fail.nix", "--log-lines", "0"]).run()
        result.expect(1)
        assert "builder for" in result.stderr_plain
        assert "log lines:" not in result.stderr_plain
        assert "line-30" not in result.stderr_plain
        _assert_not_restricted(result, "log-lines")


@pytest.mark.parametrize("daemon", [NixDaemonProtocol.LIX_XP_1], indirect=True)
@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_log_lines_over_total(nix: Nix, daemon: NixDaemon):
    nix.settings["log-format"] = "raw"
    nix.settings.add_xp_feature("nix-command")
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--log-lines", "40"]).run()
        result.expect(1)
        # the build only reports 30 lines, so the daemon must clamp to what it has
        _assert_tail(result, 30)


@pytest.mark.parametrize("daemon", [NixDaemonProtocol.LIX_XP_1], indirect=True)
@with_files(
    {"config.nix": get_global_asset("config.nix"), "fail.nix": CopyFile("client-settings/fail.nix")}
)
def test_log_lines_alongside_restricted_setting(nix: Nix, daemon: NixDaemon):
    nix.settings["log-format"] = "raw"
    nix.settings.add_xp_feature("nix-command")
    with daemon(nix) as inner:
        result = inner.nix_build(["./fail.nix", "--log-lines", "5", "--no-require-sigs"]).run()
        result.expect(1)
        _assert_tail(result, 5)
        assert "Ignoring the client-specified setting 'require-sigs'" in result.stderr_plain
