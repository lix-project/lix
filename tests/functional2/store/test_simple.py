from pathlib import Path

import pytest

from testlib.fixtures.file_helper import with_files, CopyFile, File
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack
from testlib.environ import environ

_files = get_global_asset_pack("simple-drv") | {
    "hash-check.nix": CopyFile("assets/test_simple/hash-check.nix"),
    "big-derivation-attr.nix": CopyFile("assets/test_simple/big-derivation-attr.nix"),
    "dummy": File("Hello World\n"),
}


@pytest.fixture
def drv(nix: Nix) -> str:
    res = nix.nix_instantiate(["simple.nix"]).run().ok()
    return res.stdout_plain


@with_files(_files)
def test_store_system(nix: Nix, drv: str):
    res = nix.nix_store(["-q", "--binding", "system", drv]).run().ok()
    assert res.stdout_plain == environ.get("system")


@with_files(_files)
def test_out_path(nix: Nix, drv: str):
    res = nix.nix_store(["-rvv", drv]).run().ok()
    out_path = Path(res.stdout_plain)

    assert out_path.exists()
    text_path = out_path / "hello"
    assert text_path.read_text() == "Hello World!\n"

    # Directed delete: $outPath is not reachable from a root, so it should
    # be deleteable.
    nix.nix_store(["--delete", str(out_path)]).run().ok()
    assert not text_path.exists()

    res = (
        nix.nix(
            [
                "eval",
                "--store",
                f"local?store=/foo&real={nix.env.dirs.real_store_dir}",
                "--read-only",
                "-f",
                "hash-check.nix",
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    assert (
        res.stdout_plain == "«derivation /foo/lfy1s6ca46rm5r6w4gg9hc0axiakjcnm-dependencies.drv»"
    ), "hashDerivationModulo appears broken"

    nix.env.set_env("NIX_REMOTE", f"local?store=/foo&real={nix.env.dirs.real_store_dir}")
    nix.settings.store = None
    res = nix.nix_instantiate(["--readonly-mode", "hash-check.nix"]).run().ok()
    assert res.stdout_plain == "/foo/lfy1s6ca46rm5r6w4gg9hc0axiakjcnm-dependencies.drv", (
        "hashDerivationModulo appears broken"
    )

    res = nix.nix_instantiate(["--readonly-mode", "big-derivation-attr.nix"]).run().ok()
    assert res.stdout_plain == "/foo/xxiwa5zlaajv6xdjynf9yym9g319d6mn-big-derivation-attr.drv", (
        "big-derivation-attr.nix hash appears broken. Memory corruption in large drv attr?"
    )
