from pathlib import Path

import pytest

from testlib.fixtures.nix import Nix


@pytest.mark.no_daemon
@pytest.mark.parametrize("sha", ["", "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="])
def test_locked_tarball_ttl(nix: Nix, sha: str):
    files = nix.env.dirs.home

    (files / "file").write_text("contents")
    res = (
        nix.nix(
            [
                "eval",
                "--expr",
                f'builtins.fetchurl {{ url = "file://{files}/file"; sha256 = "{sha}"; }}',
            ],
            flake=True,
        )
        .run()
        .expect(102)
    )
    assert "hash mismatch" in res.stderr_plain
    assert "sha256-0bKln76n4gB3r5+Rsn6V6GUGGycL4D/1Oas7c1h4gug=" in res.stderr_plain

    # empty or zero hash should count as not locked and be refetched
    (files / "file").write_text("other contents")
    res = (
        nix.nix(
            [
                "eval",
                *["--tarball-ttl", "0"],
                "--expr",
                f'builtins.fetchurl {{ url = "file://{files}/file"; sha256 = "{sha}"; }}',
            ],
            flake=True,
        )
        .run()
        .expect(102)
    )
    assert "hash mismatch" in res.stderr_plain
    assert "sha256-Ql7LXhAAyVRbEF8VR8bW8D0c1LONlNRX7AMpB+i1EHk=" in res.stderr_plain

    # hash-locked tarballs should *not* be refetched as long as their store path exists
    (files / "file").write_text("different contents")
    res = (
        nix.nix(
            [
                "eval",
                *["--tarball-ttl", "0"],
                "--raw",
                "--expr",
                f'builtins.fetchurl {{ url = "file://{files}/file"; sha256 = "sha256-Ql7LXhAAyVRbEF8VR8bW8D0c1LONlNRX7AMpB+i1EHk="; }}',
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    path = Path(res.stdout_plain)
    assert path.read_text() == "other contents"

    # but store paths that went away are always refetched and checked
    nix.nix(["store", "delete", path], flake=True).run().ok()
    res = (
        nix.nix(
            [
                "eval",
                "--expr",
                f'builtins.fetchurl {{ url = "file://{files}/file"; sha256 = "sha256-Ql7LXhAAyVRbEF8VR8bW8D0c1LONlNRX7AMpB+i1EHk="; }}',
            ],
            flake=True,
        )
        .run()
        .expect(102)
    )
    assert "hash mismatch" in res.stderr_plain
    assert "sha256-DKElpSdXX/9t9X/8f4IfbwQccBptZZqVy1/6JW6uFoY=" in res.stderr_plain
