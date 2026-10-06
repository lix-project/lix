from testlib.fixtures.command import CommandResult
from testlib.fixtures.file_helper import CopyFile, with_files
from testlib.fixtures.nix import Nix, NixDaemon
from testlib.utils import get_global_asset


def _assert_not_restricted(result: CommandResult, name: str):
    assert f"Ignoring the client-specified setting '{name}'" not in result.stderr_plain
    assert f"Ignoring the client-specified preference '{name}'" not in result.stderr_plain


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
