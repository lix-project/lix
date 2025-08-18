import pytest

from functional2.testlib.fixtures.env import ManagedEnv
from functional2.testlib.fixtures.file_helper import with_files
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset


@with_files({"trivial.nix": get_global_asset("trivial.nix")})
@pytest.mark.parametrize("prefix", ["by-absolute-path", "by-relative-path"])
def test_nix_path(env: ManagedEnv, nix: Nix, prefix: str):
    env.set_env(
        "NIX_PATH",
        f"non-existent=/non-existent/but-unused-anyways:by-absolute-path={env.dirs.home}:by-relative-path=.",
    )

    expected_path = str(env.dirs.home / "trivial.nix")

    res = (
        nix.nix_instantiate(["--eval", "-E", f"<{prefix}/trivial.nix>", "--restrict-eval"])
        .run()
        .ok()
    )
    assert res.stdout_plain == expected_path
    res = nix.nix_instantiate(["--find-file", f"{prefix}/trivial.nix"]).run().ok()
    path = res.stdout_plain
    assert path == expected_path


@pytest.mark.skip(
    "FIXME(Commentator2.0): for some reason the channel cannot be resolved on the builders"
)
def test_nix_path_nixpkgs(env: ManagedEnv, nix: Nix):
    env.set_env(
        "NIX_PATH",
        f"non-existent=/non-existent/but-unused-anyways:by-absolute-path={env.dirs.home}:by-relative-path=.",
    )
    res = (
        nix.nix_instantiate(
            ["--eval", "-E", "<nixpkgs>", "-I", "nixpkgs=channel:nixos-unstable", "--restrict-eval"]
        )
        .run()
        .ok()
    )
    assert str(env.dirs.nix_store_dir) in res.stdout_plain
