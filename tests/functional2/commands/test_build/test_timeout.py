from functional2.testlib.fixtures.file_helper import with_files, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset

_files = {
    "timeout.nix": CopyFile("assets/test_timeout/timeout.nix"),
    "config.nix": get_global_asset("config.nix"),
}


@with_files(_files)
def test_timeout_timeout(nix: Nix):
    res = (
        nix.nix_build(["-Q", "timeout.nix", "-A", "infiniteLoop", "--timeout", "2"])
        .run()
        .expect(101)
    )
    assert "timed out" in res.stderr_plain


@with_files(_files)
def test_timeout_max_log(nix: Nix):
    res = (
        nix.nix_build(["-Q", "timeout.nix", "-A", "infiniteLoop", "--max-build-log-size", "100"])
        .run()
        .expect(1)
    )
    assert "killed after writing more than 100 bytes of log output" in res.stderr_plain


@with_files(_files)
def test_timeout_silent(nix: Nix):
    res = nix.nix_build(["timeout.nix", "-A", "silent", "--max-silent-time", "2"]).run().expect(101)
    assert "file timed out after 2 seconds of silence" in res.stderr_plain


@with_files(_files)
def test_timeout_close_log(nix: Nix):
    res = nix.nix_build(["timeout.nix", "-A", "closeLog"]).run().expect(100)
    assert "failed due to signal 9 (Killed" in res.stderr_plain


@with_files(_files)
def test_timeout_nix3_silent(nix: Nix):
    res = (
        nix.nix(["build", "-f", "timeout.nix", "silent", "--max-silent-time", "2"], flake=True)
        .run()
        .expect(1)
    )
    assert "from .drv file timed out after 2 seconds of silence" in res.stderr_plain
