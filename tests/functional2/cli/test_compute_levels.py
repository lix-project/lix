import os
import sys
import pytest
from testlib.fixtures.nix import Nix


@pytest.mark.skipif(
    sys.platform != "linux" or os.uname().machine != "x86_64", reason="uarch levels are x86_64 only"
)
def test_compute_levels(nix: Nix):
    assert "x86_64-v1-linux" in nix.nix(["-vv", "--version"]).run().ok().stdout_plain
