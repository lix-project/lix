import re

from functional2.testlib.fixtures.file_helper import with_files, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset

_fod_files = {
    "fod-failing.nix": CopyFile("assets/test_build/fod-failing.nix"),
    "config.nix": get_global_asset("config.nix"),
}
_build_args = ["build", "-f", "fod-failing.nix", "-L"]


@with_files(_fod_files)
def test_url_mismatch(nix: Nix):
    res = nix.nix_build(["fod-failing.nix", "-A", "x1"]).run().expect(102)
    err = res.stderr_plain
    assert len(re.findall(r"hash mismatch in fixed-output derivation '.*-x1\.drv'", err)) == 1
    assert "likely URL: https://meow.puppy.forge/puppy.tar.gz" in err


@with_files(_fod_files)
def test_url_keep_going_1(nix: Nix):
    res = nix.nix([*_build_args], flake=True).run().expect(1)
    err = res.stderr_plain
    assert err.count("error:") >= 2
    assert re.findall(r"hash mismatch in fixed-output derivation '.*-x.\.drv'", err)
    assert "likely URL: " in err
    assert re.findall(
        r"error: build of '.*-x[1-4]\.drv\^out', '.*-x[1-4]\.drv\^out', '.*-x[1-4]\.drv\^out', '.*-x[1-4]\.drv\^out' failed",
        err,
    )


@with_files(_fod_files)
def test_url_keep_going_2(nix: Nix):
    res = nix.nix([*_build_args, "x1", "x2", "x3", "--keep-going"], flake=True).run().expect(1)
    err = res.stderr_plain
    assert err.count("error:") == 4
    for f in ["x1", "x2", "x3"]:
        assert re.findall(rf"hash mismatch in fixed-output derivation '.*-{f}\.drv'", err)
    assert "likely URL: https://meow.puppy.forge/puppy.tar.gz" in err
    assert "likely URL: https://kitty.forge/cat.tar.gz" in err
    assert "likely URL: (unknown)" in err
    assert re.findall(
        r"error: build of '.*-x[1-3]\.drv\^out', '.*-x[1-3]\.drv\^out', '.*-x[1-3]\.drv\^out' failed",
        err,
    )


@with_files(_fod_files)
def test_missing_dependency(nix: Nix):
    res = nix.nix([*_build_args, "x4"], flake=True).run().expect(1)
    err = res.stderr_plain
    assert err.count("error:") >= 2
    assert re.findall(r"error: [12] dependencies of derivation '.*-x4\.drv' failed to build", err)
    assert re.findall(r"hash mismatch in fixed-output derivation '.*-x[23]\.drv'", err)


@with_files(_fod_files)
def test_missing_dependency_keep_going(nix: Nix):
    res = nix.nix([*_build_args, "x4", "--keep-going"], flake=True).run().expect(1)
    err = res.stderr_plain
    assert err.count("\nerror:") == 3
    assert re.findall(r"error: 2 dependencies of derivation '.*-x4\.drv' failed to build", err)
    for f in ["x2", "x3"]:
        assert re.findall(rf"hash mismatch in fixed-output derivation '.*-{f}\.drv'", err)


@with_files({"config.nix": get_global_asset("config.nix")})
def test_build_inaccessible_build_dir(nix: Nix):
    env = nix.env
    new_build_dir = env.dirs.home / "build-dir"
    new_build_dir.mkdir()
    new_build_dir.chmod(0o0000)
    try:
        nix.nix(
            [
                "--build-dir",
                str(new_build_dir),
                "build",
                "-E",
                'with import ./config.nix; mkDerivation { name = "test"; buildCommand = "echo rawr > $out"; }',
                "--impure",
                "--no-link",
            ],
            flake=True,
            build=True,
        ).run().ok()
    finally:
        # clean up perms
        new_build_dir.chmod(0o777)
