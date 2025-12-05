import pytest
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import with_files, File


drv = """
  builtins.derivation {
    name = "foo";
    builder = /bin/sh;
    system = builtins.currentSystem;
    requiredSystemFeatures = [ "glitter" ];
  }
"""

local_drv = """
  with import ./config.nix; mkDerivation {
    name = "foo";
    buildCommand = ''
      touch $out
    '';
    preferLocalBuild = true;
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


@with_files({"config.nix": get_global_asset("config.nix"), "default.nix": File(local_drv)})
def test_j0_without_local_jobs(nix: Nix):
    result = nix.nix_build(["-j0", "--extra-local-jobs", "0"]).run().expect(1)
    assert (
        "error: unable to start any build; either set '--max-jobs' to a non-zero value or enable remote builds."
        in result.stderr_s
    )


@with_files({"config.nix": get_global_asset("config.nix"), "default.nix": File(local_drv)})
def test_j0_with_local_jobs(nix: Nix):
    nix.nix_build(["-j0"]).run().ok()


# TODO(Raito): test -j1 --extra-local-jobs=1 in another file and ensure that preferLocalBuild *AND* another build job can run properly at the same time.
