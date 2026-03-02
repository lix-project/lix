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
