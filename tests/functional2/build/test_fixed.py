import re
from pathlib import Path

import pytest

from testlib.fixtures.env import ManagedEnv
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack


@pytest.fixture(autouse=True)
def impure_vars(env: ManagedEnv):
    """
    Sets the IMPURE_VARs required by fixed.builder.sh
    because I have no clue what Eelco was thinking when writing this stuff
    """
    env["IMPURE_VAR1"] = "foo"
    env["IMPURE_VAR2"] = "bar"


@with_files(get_global_asset_pack("fixed"))
def test_bad(nix: Nix):
    res = nix.nix_instantiate(["fixed.nix", "-A", "good.0"], build=True).run().ok()
    store_path = nix.nix_store(["-q", res.stdout_plain], build=True).run().ok().stdout_plain
    path = Path(store_path)
    assert not path.exists()

    # Building with the bad hash should produce the "good" output path as
    # a sideeffect.
    res = nix.nix_build(["fixed.nix", "-A", "bad", "--no-out-link"]).run().expect(102)
    assert "hash mismatch in fixed-output derivation" in res.stderr_plain
    assert path.exists()

    nix.settings.add_xp_feature("nix-command")
    res = nix.nix(["path-info", "--json", store_path], build=True).run().ok()
    assert res.json()[0]["ca"] == "fixed:md5:2qk15sxzzjlnpjk9brn7j8ppcd"


@with_files(get_global_asset_pack("fixed"))
def test_good(nix: Nix):
    nix.nix_build(["fixed.nix", "-A", "good", "--no-out-link"]).run().ok()


@with_files(get_global_asset_pack("fixed"))
def test_check(nix: Nix):
    res = nix.nix_build(["fixed.nix", "-A", "check", "--check"]).run().expect(1)
    assert "has no valid outputs registered in the store" in res.stderr_plain


@with_files(get_global_asset_pack("fixed"))
def test_good2(nix: Nix):
    # we need to build good.0 *first*, otherwise the hash good2 wants to be materialized
    # to does not exist. if it does not exist good2 would be built and *fail* since we'd
    # build a non-flat-hashable output. this is mondo jacked, but what can we do. -sigh-
    nix.nix_build(["fixed.nix", "-A", "good.0", "--no-out-link"]).run().ok()
    nix.nix_build(["fixed.nix", "-A", "good2", "--no-out-link"]).run().ok()


@with_files(get_global_asset_pack("fixed"))
def test_really_bad(nix: Nix):
    res = nix.nix_instantiate(["fixed.nix", "-A", "reallyBad"]).run().expect(1)
    assert (
        "error: hash 'ddd8be4b179a529afa5f2ffae4b9858' has wrong length for hash type 'md5'"
        in res.stderr_plain
    )


@with_files(get_global_asset_pack("fixed"))
def test_other_store_references(nix: Nix):
    res = nix.nix_build(["fixed.nix", "-A", "badReferences"]).run().expect(1)
    assert "not allowed to refer to other store paths" in res.stderr_plain


@with_files(get_global_asset_pack("fixed"))
def test_illegal_references(nix: Nix):
    """
    Fixed FOD hashes cannot be asserted because:
    - the store directory varies between the "Lix build sandbox environment" and a user test run
    - *-darwin has a different store location on the top of this in the sandbox (/private/tmp/...) causing further changes.
    Regex matching is the best we can afford.
    """
    res = nix.nix_build(["fixed.nix", "-A", "illegalReferences"]).run().expect(102)
    assert re.findall(
        r"the fixed-output derivation '.*?/nix/store/[a-z0-9]*-illegal-reference.drv' must not reference store paths but 1 such references were found:.*/nix/store/[a-z0-9]*-fixed",
        res.stderr_plain,
        re.DOTALL,
    )


@with_files(get_global_asset_pack("fixed"))
def test_attribute_selection(nix: Nix):
    nix.nix_instantiate(["fixed.nix", "-A", "good.1"]).run().ok()


@with_files(get_global_asset_pack("fixed"))
def test_parallel_same(nix: Nix):
    """
    Test parallel builds of derivations that produce the same output.
    Only one should run at the same time.
    """
    nix.nix_build(["fixed.nix", "-A", "parallelSame", "--no-out-link", "-j2"]).run().ok()


@pytest.mark.skip("TODO(rootile, 2025-10): Doesn't work for some reason, f1 test is kept for now")
@with_files(get_global_asset_pack("fixed"))
def test_same_as_add(nix: Nix, files: Path):
    """
    Fixed-output derivations with a recursive SHA-256 hash should
    produce the same path as "nix-store --add".
    """
    res = nix.nix_build(["fixed.nix", "-A", "sameAsAdd", "--no-out-link"]).run().ok()
    out = res.stdout_plain

    fixed = files / "fixed"
    # is failing as this isn't created for f2: shutil.rmtree(fixed)
    (fixed / "bla").mkdir(parents=True)
    foo = fixed / "foo"
    foo.write_text("Hello World!")
    (fixed / "bar").symlink_to(foo)

    out2 = nix.nix_store(["--add", str(fixed)]).run().ok().stdout_plain
    assert out == out2

    out3 = (
        nix.nix_store(["--add-fixed", "--recursive", "sha256", str(fixed)]).run().ok().stdout_plain
    )
    assert out == out3

    out4 = (
        nix.nix_store(
            [
                "--print-fixed-path",
                "--recursive",
                "sha256",
                "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik",
                "fixed",
            ]
        )
        .run()
        .ok()
        .stdout_plain
    )
    assert out == out4
