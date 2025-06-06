from functional2.testlib.fixtures.file_helper import with_files
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("dependencies"))
def test_command_doesnt_crash(nix: Nix):
    nix.nix_store(["--init"]).run().ok()
    nix.nix(
        ["why-depends", "--derivation", "--file", "./dependencies.nix", "input2_drv", "input1_drv"],
        flake=True,
        build=True,
    ).run().ok()
    nix.nix(
        ["why-depends", "--file", "./dependencies.nix", "input2_drv", "input1_drv"],
        flake=True,
        build=True,
    ).run().ok()


def built_files(nix: Nix):
    nix.nix_build(["./dependencies.nix", "-A", "input0_drv", "-o", "dep"]).run().ok()
    nix.nix_build(["./dependencies.nix", "-A", "input3_drv", "-o", "dep3"]).run().ok()
    nix.nix_build(["./dependencies.nix", "-o", "toplevel"]).run().ok()


@with_files(get_global_asset_pack("dependencies"))
def test_fast_depends(nix: Nix):
    built_files(nix)
    res = nix.nix(["why-depends", "./toplevel", "./dep"], flake=True, build=True).run().ok()
    assert "input-2" in res.stdout_plain
    assert "reference-to-input-2" not in res.stdout_plain


@with_files(get_global_asset_pack("dependencies"))
def test_precise_depends(nix: Nix):
    built_files(nix)
    res = (
        nix.nix(["why-depends", "./toplevel", "./dep", "--precise"], flake=True, build=True)
        .run()
        .ok()
    )
    out = res.stdout_plain
    assert "input-2" in out
    assert "reference-to-input-2" in out
    lines = out.splitlines()
    assert "reference-to-input-2 -> " in lines[1]
    assert "dependencies-input-2" in lines[2]
    assert "input0: " in lines[3]
    assert "dependencies-input-0" in lines[4]


@with_files(get_global_asset_pack("dependencies"))
def test_self_ref(nix: Nix):
    built_files(nix)
    res = nix.nix(["why-depends", "./toplevel", "./toplevel"], flake=True, build=True).run().ok()
    lines = res.stdout_plain.splitlines()
    assert "dependencies-top" in lines[0]
    assert "dependencies-top" in lines[1]


@with_files(get_global_asset_pack("dependencies"))
def test_all(nix: Nix):
    built_files(nix)
    res = (
        nix.nix(["why-depends", "./toplevel", "./dep3", "--all"], flake=True, build=True).run().ok()
    )
    lines = res.stdout_plain.splitlines()
    assert "dependencies-top" in lines[0]
    assert "input-2" in lines[1]
    assert "input-3" in lines[2]
