from pathlib import Path
from textwrap import dedent
from testlib.fixtures.file_helper import File
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
import pytest


pytestmark = pytest.mark.no_daemon


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_local_store(nix: Nix):
    res = nix.nix(["doctor", "-v"]).run().ok()
    out = res.stderr_plain
    assert "[PASS] All profiles are gcroots." in out
    assert "[PASS] Client protocol matches store protocol." in out
    assert "You are trusted" in out
    assert "[FAIL] Error: current generation cannot be discovered" in out


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_remote_store(nix: Nix):
    res = (
        nix.nix(
            [
                "--store",
                f"ssh-ng://localhost?remote-store={nix.env.dirs.test_root}/other-store",
                "doctor",
            ]
        )
        .run()
        .ok()
    )
    assert "Running checks against store uri ssh-ng://localhost" in res.stderr_plain


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_nixpkgs(nix: Nix):
    res = nix.nix(["doctor", "-v"], flake=True).run().ok()
    out = res.stderr_plain

    assert "[INFO] Nixpkgs provenance: nixpkgs=" in out


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_no_builders(nix: Nix):
    res = nix.nix(["doctor"]).run().ok()
    assert "[INFO] no remote builders found" in res.stderr_plain


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_checks_builder_bad(nix: Nix):
    nix.settings.builders = "[machines.hello]"
    res = nix.nix(["doctor"]).run().expect(2)

    assert "[FAIL] invalid remote builders configuration" in res.stderr_plain


@with_files(
    {
        "machines.toml": File(
            dedent("""
                [machines.andesite]
                uri = "dummy://"
            """)
        )
    }
)
@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_checks_builder_connection_flag(nix: Nix, files: Path):
    nix.settings.builders = f"@{files / 'machines.toml'}"
    res = nix.nix(["doctor", "-v", "--check-remotes"]).run().expect(2)
    out = res.stderr_plain

    assert "[INFO] 1 remote builder(s) configured" in out
    assert "Running checks against store uri dummy" in out
    assert "Store protocol: unknown" in out


@with_files(
    {
        "machines.toml": File(
            dedent("""
                [machines.andesite]
                uri = "bad://connection"
                [machines.diorite]
                uri = "another://bad.connection"
            """)
        )
    }
)
@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_builders_bad_connection(nix: Nix, files: Path):
    nix.settings.builders = f"@{files / 'machines.toml'}"
    res = nix.nix(["doctor", "-v", "--check-remotes"]).run().expect(2)
    out = res.stderr_plain

    assert "attempting connection to andesite" in out
    assert "attempting connection to diorite" in out
    assert (
        "[FAIL] connection failed: error: don't know how to open Nix store 'bad://connection'"
        in out
    )
    assert (
        "[FAIL] connection failed: error: don't know how to open Nix store 'another://bad.connection'"
        in out
    )


@with_files(
    {
        "machines.toml": File(
            dedent("""
                [machines.andesite]
                uri = "dummy://"
            """)
        )
    }
)
@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_no_check_connection(nix: Nix, files: Path):
    nix.settings.builders = f"@{files / 'machines.toml'}"
    res = nix.nix(["doctor", "-v"]).run().ok()
    out = res.stderr_plain

    assert "[INFO] 1 remote builder(s) configured" in out
    assert "Running checks against store uri dummy" not in out
    assert "Store protocol: unknown" not in out


@pytest.mark.usefixtures("fake_nixpkgs")
def test_doctor_dot_in_path(nix: Nix):
    """Haveing `.` in the PATH used to crash nix doctor with "not an absolute path" """
    nix.env.path.prepend(".")
    nix.nix(["doctor", "-v"]).run().ok()
