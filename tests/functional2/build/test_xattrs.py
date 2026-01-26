import pytest
from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset, CopyFile
from testlib.xattrs import verify_no_xattrs_in_tree, skip_if_xattrs_are_unsupported

# NOTE(Raito): xattrs are forbidden in builds for the time being.
# See: https://zulip.lix.systems/#narrow/channel/9-Store/topic/disablement.20of.20xattrs/with/5295 for the rationale.
# Once these hurddles are cleared, remove the skip markers.


@with_files(
    {"config.nix": get_global_asset("config.nix"), "xattrs.nix": CopyFile("assets/xattrs.nix")}
)
@pytest.mark.skip(reason="xattrs are forbidden in builds")
def test_xattrs_during_build(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    nix.nix_build(["xattrs.nix", "-A", "during-build", "--no-out-link"]).run().ok()


def build_and_get_store_path(nix: Nix, asset: str, attribute: str) -> Path:
    return nix.physical_store_path_for(
        nix.nix_build([asset, "-A", attribute, "--no-out-link"]).run().ok().stdout_plain
    )


@with_files(
    {"config.nix": get_global_asset("config.nix"), "xattrs.nix": CopyFile("assets/xattrs.nix")}
)
@pytest.mark.skip(reason="xattrs are forbidden in builds")
def test_xattrs_in_output(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    # We assert that xattrs producing derivations in the outputs should complete with no xattrs in the final output path.
    # NOTE: if another platform is added, another `verify_no_acl_in_tree`
    # should be added to ensure that ACLs are truly removed.
    # On Linux, this is not necessary.
    output_path = build_and_get_store_path(nix, "xattrs.nix", "in-root-outputs-file")
    verify_no_xattrs_in_tree(output_path)

    output_path = build_and_get_store_path(nix, "xattrs.nix", "in-root-outputs-dir")
    verify_no_xattrs_in_tree(output_path)

    output_path = build_and_get_store_path(nix, "xattrs.nix", "in-output-content")
    verify_no_xattrs_in_tree(output_path)
