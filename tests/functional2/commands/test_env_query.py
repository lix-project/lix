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
