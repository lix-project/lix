from testlib.fixtures.nix import Nix
import pytest


pytestmark = pytest.mark.no_daemon


@pytest.mark.parametrize(
    "args",
    [
        ["--to-sri", "--type", "sha256", "aa" * 32],
        ["--to-sri", "sha256:" + "aa" * 32],
        ["--type", "sha256", "--sri", "."],
    ],
)
def test_wont_truncate_sri(nix: Nix, args: list[str]):
    result = nix.nix(["--truncate", *args], nix_exe="nix-hash").run().expect(1)
    assert "cannot truncate SRI hashes" in result.stderr_plain
    assert "--help" not in result.stderr_plain
