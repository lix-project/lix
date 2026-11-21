from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import merge_file_declaration
from testlib.fixtures.env import ManagedEnv
from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset_pack
from testlib.fixtures.nix import Nix
import pytest


def drv_names(store: Path) -> set[str]:
    return {p.name.partition("-")[2] for p in store.glob("*.drv")}


def outputs(store: Path) -> list[Path]:
    return [d for d in store.glob("*-*") if d.is_dir()]


def output_names(store: Path) -> set[str]:
    return {p.name.partition("-")[2] for p in outputs(store)}


def local_drvs(env: ManagedEnv) -> set[str]:
    return drv_names(env.dirs.nix_store_dir)


def local_outputs(env: ManagedEnv) -> list[Path]:
    return output_names(env.dirs.nix_store_dir)


def eval_store_drvs(env: ManagedEnv) -> set[str]:
    return drv_names(env.dirs.home / "eval_store" / "nix" / "store")


def eval_store_outputs(env: ManagedEnv) -> list[Path]:
    return output_names(env.dirs.home / "eval_store" / "nix" / "store")


@pytest.mark.no_daemon
@with_files(get_global_asset_pack("dependencies"))
def test_nix3_build(nix: Nix, files: Path):
    eval_store = files / "eval_store"
    res_link = files / "result"

    nix.nix(
        ["build", "-f", "dependencies.nix", "--eval-store", eval_store, "-o", res_link], flake=True
    ).run().ok()
    assert (res_link / "foobar").exists()

    assert local_drvs(nix.env) == eval_store_drvs(nix.env)
    assert local_outputs(nix.env) == {
        "dependencies-input-0",
        "dependencies-input-1",
        "dependencies-input-2",
        "dependencies-input-3",
        "dependencies-top",
    }
    assert not eval_store_outputs(nix.env)


@with_files(get_global_asset_pack("dependencies"))
def test_nix_instantiate(nix: Nix, files: Path):
    nix.nix_instantiate(["dependencies.nix", "--eval-store", files / "eval_store"]).run().ok()
    assert not local_drvs(nix.env)
    assert not local_outputs(nix.env)
    assert eval_store_drvs(nix.env) == {
        "dependencies-top.drv",
        "dependencies-input-2.drv",
        "dependencies-input-3.drv",
        "fod-input.drv",
        "dependencies-input-0.drv",
        "dependencies-input-1.drv",
    }
    assert not eval_store_outputs(nix.env)


@pytest.mark.no_daemon
@with_files(get_global_asset_pack("dependencies"))
def test_nix_build(nix: Nix, files: Path):
    res_link = files / "result"
    nix.nix_build(
        ["dependencies.nix", "--eval-store", files / "eval_store", "-o", res_link]
    ).run().ok()
    assert (res_link / "foobar").exists()

    assert local_drvs(nix.env) == eval_store_drvs(nix.env)
    assert local_outputs(nix.env) == {
        "dependencies-input-0",
        "dependencies-input-1",
        "dependencies-input-2",
        "dependencies-input-3",
        "dependencies-top",
    }
    assert not eval_store_outputs(nix.env)


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


@pytest.mark.no_daemon
@with_files(get_global_asset_pack("dependencies"))
def test_nix_build_with_explicit_drv(nix: Nix, files: Path):
    """
    test that building a /some/thing.drv^output works with an eval store,
    even when the eval store does not have the drv being built. we should
    never try to copy files from an eval store that aren't present there.
    """
    nix.settings.add_xp_feature("nix-command")

    drv = nix.nix_instantiate(["dependencies.nix"]).run().ok().stdout_plain
    nix.nix(["build", f"{drv}^out", "--eval-store", f"{files}/other-store"]).run().ok()

    assert (nix.env.dirs.home / "result/foobar").exists()
