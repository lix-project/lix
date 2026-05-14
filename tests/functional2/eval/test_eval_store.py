from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import merge_file_declaration
from testlib.fixtures.env import ManagedEnv
from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from testlib.fixtures.nix import Nix
import pytest


def assert_only_scratch_drvs(env: ManagedEnv):
    assert not list(env.dirs.nix_store_dir.glob("*.drv"))
    assert list((env.dirs.home / "eval_store" / "nix" / "store").glob("*.drv"))


@pytest.mark.no_daemon
@with_files(get_global_asset_pack("dependencies"))
def test_nix3_build(nix: Nix, files: Path):
    eval_store = files / "eval_store"
    res_link = files / "result"

    nix.nix(
        ["build", "-f", "dependencies.nix", "--eval-store", eval_store, "-o", res_link], flake=True
    ).run().ok()
    assert (res_link / "foobar").exists()

    assert_only_scratch_drvs(nix.env)


@with_files(get_global_asset_pack("dependencies"))
def test_nix_instantiate(nix: Nix, files: Path):
    nix.nix_instantiate(["dependencies.nix", "--eval-store", files / "eval_store"]).run().ok()
    assert_only_scratch_drvs(nix.env)


@pytest.mark.no_daemon
@with_files(get_global_asset_pack("dependencies"))
def test_nix_build(nix: Nix, files: Path):
    res_link = files / "result"
    nix.nix_build(
        ["dependencies.nix", "--eval-store", files / "eval_store", "-o", res_link]
    ).run().ok()
    assert (res_link / "foobar").exists()
    assert_only_scratch_drvs(nix.env)


@with_files(
    merge_file_declaration(
        get_global_asset_pack("dependencies"), {"ifd.nix": get_global_asset("ifd.nix")}
    )
)
def test_import_from_derivation_builds(nix: Nix, files: Path):
    eval_store = files / "eval_store"
    res = (
        nix.nix(
            [
                "eval",
                "--eval-store",
                f"{eval_store}?require-sigs=false",
                "--impure",
                "--raw",
                "--file",
                "./ifd.nix",
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "hi"
    assert list(nix.env.dirs.nix_store_dir.glob("*dependencies-top/foobar"))
    assert not list((files / "nix" / "store").glob("*dependencies-top/foobar"))
