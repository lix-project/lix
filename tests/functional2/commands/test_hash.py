from pathlib import Path
from collections.abc import Callable

import pytest

from functional2.testlib.fixtures.file_helper import (
    _init_files,
    File,
    AssetSymlink,
    with_files,
    Symlink,
)
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@pytest.mark.parametrize("algo", ["md5", "sha1", "sha256", "sha512"])
@pytest.mark.parametrize(
    "content",
    ["", "a", "abc", "message_digest", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"],
)
@pytest.mark.parametrize("format_flag", ["--base16", "--base32", "--sri"])
@pytest.mark.parametrize("classic", [True, False])
def test_hash(
    algo: str,
    content: str,
    format_flag: str,
    classic: bool,
    nix: Nix,
    snapshot: Callable[[str], Snapshot],
    files: Path,
):
    _init_files(
        {
            "vector": File(content),
            "expected_hash.txt": AssetSymlink(
                f"assets/test_hash/{content}_{algo}_{format_flag}_{classic}.out.exp"
            ),
        },
        files,
        Path(__file__).parent,
    )

    args = [format_flag, "--type", algo, files / "vector"]
    if classic:
        res = nix.nix(["--flat", *args], nix_exe="nix-hash").run().ok()
    else:
        res = nix.nix(["hash", "file", *args], flake=True).run().ok()
    assert snapshot("expected_hash.txt") == res.stdout_s


@with_files({"vector": File("abc")})
def test_hash_classic_defaults_base16(nix: Nix, files: Path):
    res = nix.nix(["--flat", "--type", "sha512", files / "vector"], nix_exe="nix-hash").run().ok()
    assert (
        res.stdout_plain
        == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
    )


@with_files({"vector": File("abc")})
def test_hash_command_defaults_sri(nix: Nix, files: Path):
    res = nix.nix(["hash", "file", "--type", "sha512", files / "vector"], flake=True).run().ok()
    assert (
        res.stdout_plain
        == "sha512-3a81oZNherrMQXNJriBBMRLm+k6JqX6iCp7u5ktV05ohkpkqJ0/BqDa6PCOj/uu9RU1EI2Q86A4qmslPpUyknw=="
    )


@with_files(
    {
        "hash-part": {
            "hello": File("Hello World\n", 0o644)  # Default perms
        }
    }
)
def test_hash_part(nix: Nix, files: Path):
    res = nix.nix(["--type", "md5", files / "hash-part"], nix_exe="nix-hash").run().ok()
    assert res.stdout_plain == "ea9b55537dd4c7e104515b2ccfaf4100"


@with_files(
    {
        "hash-part": {
            "hello": File("Hello World\n", 0o755)  # chmod +x
        }
    }
)
def test_hash_part_exec_matters(nix: Nix, files: Path):
    res = nix.nix(["--type", "md5", files / "hash-part"], nix_exe="nix-hash").run().ok()
    assert res.stdout_plain == "20f3ffe011d4cfa7d72bfabef7882836"


@with_files({"hash-part": {"hello": File("Hello World\n", 0o744)}})
def test_hash_part_everything_else_doesnt(nix: Nix, files: Path):
    res = nix.nix(["--type", "md5", files / "hash-part"], nix_exe="nix-hash").run().ok()
    assert res.stdout_plain == "20f3ffe011d4cfa7d72bfabef7882836"


@with_files({"hash-part": {"hello": Symlink("x")}})
def test_hash_part_filetype_matters(nix: Nix, files: Path):
    res = nix.nix(["--type", "md5", files / "hash-part"], nix_exe="nix-hash").run().ok()
    assert res.stdout_plain == "f78b733a68f5edbdf9413899339eaa4a"


@pytest.mark.parametrize(
    ("algo", "orig", "b32", "b64"),
    [
        (
            "sha1",
            "800d59cfcd3c05e900cb4e214be48f6b886a08df",
            "vw46m23bizj4n8afrc0fj19wrp7mj3c0",
            "gA1Zz808BekAy04hS+SPa4hqCN8=",
        ),
        (
            "sha256",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s",
            "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=",
        ),
        (
            "sha512",
            "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445",
            "12k9jiq29iyqm03swfsgiw5mlqs173qazm3n7daz43infy12pyrcdf30fkk3qwv4yl2ick8yipc2mqnlh48xsvvxl60lbx8vp38yji0",
            "IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ==",
        ),
    ],
)
def test_hash_conversion(algo: str, orig: str, b32: str, b64: str, nix: Nix):
    for target_type, exp in zip(
        ["to-base16", "to-base32", "to-base64", "to-sri"], [orig, b32, b64, f"{algo}-{b64}"]
    ):
        res = nix.nix(["--type", algo, f"--{target_type}", orig], nix_exe="nix-hash").run().ok()
        assert res.stdout_plain == exp
        res = nix.nix(["hash", target_type, "--type", algo, orig], flake=True).run().ok()
        assert res.stdout_plain == exp
