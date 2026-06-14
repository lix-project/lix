from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from testlib.fixtures.nix import Nix


@with_files(get_global_asset_pack("dependencies"))
def test_no_remote(nix: Nix):
    # Fails without remote builders
    res = (
        nix.nix_build(["--store", f"file://{nix.env.dirs.cache_dir}", "dependencies.nix"])
        .run()
        .expect(1)
    )

    assert "unable to build with a primary store" in res.stderr_plain


@with_files(get_global_asset_pack("dependencies"))
def test_success_default_store(nix: Nix):
    store = f"file://{nix.env.dirs.cache_dir}"
    # Succeeds with default store as build remote.
    res = (
        nix.nix_build(["--store", store, "--builders", "auto - - 1 1", "-j0", "dependencies.nix"])
        .run()
        .ok()
    )
    out_path = res.stdout_plain

    # Test that the path exactly exists in the destination store.
    res = nix.nix(["path-info", "--store", store, out_path], flake=True).run().ok()
    assert "dependencies-top" in res.stdout_plain

    # Succeeds without any build capability because no-op
    nix.nix_build(["--store", store, "-j0", "dependencies.nix"]).run().ok()
