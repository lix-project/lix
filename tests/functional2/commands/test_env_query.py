from pathlib import Path
from xml.etree import ElementTree

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("dependencies"))
def test_correct_xml(nix: Nix):
    """Test that nix-env --query --xml produces valid XML."""
    query_output = (
        nix.nix_env(
            ["--query", "--available", "--attr-path", "--xml", "--file", "./dependencies.nix"]
        )
        .run()
        .ok()
        .stdout_plain
    )

    # Should parse without raising.
    parsed = ElementTree.fromstring(query_output)
    assert parsed.tag == "items"


@with_files(get_global_asset_pack("dependencies"))
def test_status(nix: Nix):
    """Test that ought to cover `Store::querySubstitutablePaths`"""
    nix.nix_env(["--query", "--available", "--status", "--file", "./dependencies.nix"]).run().ok()


@with_files(get_global_asset_pack("dependencies"))
def test_referrers(nix: Nix):
    """Test that ought to cover `NixStore::queryReferrers`"""
    input2 = nix.nix_build(["./dependencies.nix", "-A", "input2_drv"]).run().ok().stdout_plain
    input0 = nix.nix_build(["./dependencies.nix", "-A", "input0_drv"]).run().ok().stdout_plain

    assert nix.nix_store(["--query", "--referrers", input0]).run().ok().stdout_plain == input2


@with_files(get_global_asset_pack("dependencies"))
def test_referrers_closure(nix: Nix):
    """Test that ought to cover `NixStore::queryReferrers` for closure query"""
    input2 = nix.nix_build(["./dependencies.nix", "-A", "input2_drv"]).run().ok().stdout_plain
    input0 = nix.nix_build(["./dependencies.nix", "-A", "input0_drv"]).run().ok().stdout_plain

    referrers = nix.nix_store(["--query", "--referrers-closure", input0]).run().ok().stdout_plain
    assert input0 in referrers
    assert input2 in referrers


@with_files({"simple": get_global_asset_pack("simple-drv")})
def test_derivers(nix: Nix, files: Path):
    """Test that ought to cover `NixStore::queryValidDerivers`"""
    drv_path = nix.nix_instantiate([f"{files}/simple/simple.nix"]).run().ok().stdout_plain
    store_path = nix.nix_build([f"{files}/simple/simple.nix"]).run().ok().stdout_plain

    assert (
        nix.nix_store(["--query", "--valid-derivers", store_path]).run().ok().stdout_plain
        == drv_path
    )
