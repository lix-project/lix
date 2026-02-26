import pytest
from testlib.fixtures.nix import Nix


drv = """
  builtins.derivation {
    name = "foo";
    builder = /bin/sh;
    system = builtins.currentSystem;
    requiredSystemFeatures = [ "glitter" ];
  }
"""


@pytest.fixture(autouse=True)
def set_features(nix: Nix):
    nix.settings.system_features = "glitter"


def test_j0_without_remotes_fails(nix: Nix):
    result = nix.nix_build(["-j0", "--expr", drv, "--builders", ""]).run().expect(1)
    assert (
        "error: unable to start any build; either set '--max-jobs' to a non-zero value or enable remote builds."
        in result.stderr_s
    )


def test_j0_with_mismatched_remotes_fails(nix: Nix):
    remote = f"ssh://localhost?remote-store={nix.env.dirs.home}/machine1"
    result = nix.nix_build(["-j0", "--expr", drv, "--builders", remote]).run().expect(1)
    assert (
        "error: unable to start any build; remote machines may not have all required system features."
        in result.stderr_s
    )
