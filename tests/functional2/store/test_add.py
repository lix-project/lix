import pytest

from testlib.fixtures.file_helper import with_files, File, AssetSymlink, Symlink
from testlib.fixtures.nix import Nix


@with_files({"file-link": AssetSymlink("./test_add.py"), "dir-link": Symlink(".")})
@pytest.mark.parametrize("hash_fn", ["sha1", "sha256"])
@pytest.mark.parametrize("path", [".", "./file-link", "dir-link"])
def test_invalid_flat_add(nix: Nix, hash_fn: str, path: str):
    cmd = nix.nix_store(["--add-fixed", hash_fn, path])
    res = cmd.run().expect(1)
    assert (
        f"cannot import {'directory' if path == '.' else 'symlink'} using flat ingestion"
        in res.stderr_s
    )


@pytest.fixture
def blank_add_path(nix: Nix) -> str:
    return nix.nix_store(["--add", "./dummy"]).run().ok().stdout_plain


@with_files({"dummy": File("Hello World\n")})
def test_add_fixed(nix: Nix, blank_add_path: str):
    args = ["--add-fixed", "sha256", "./dummy"]
    res = nix.nix_store(args).run().ok()
    assert res.stdout_plain != blank_add_path


@with_files({"dummy": File("Hello World\n")})
def test_add_fixed_rec(nix: Nix, blank_add_path: str):
    args = ["--add-fixed", "sha256", "--recursive", "./dummy"]
    res = nix.nix_store(args).run().ok()
    assert res.stdout_plain == blank_add_path


@with_files({"dummy": File("Hello World\n")})
def test_add_fixed_sha1(nix: Nix, blank_add_path: str):
    res = nix.nix_store(["--add-fixed", "sha1", "--recursive", "./dummy"]).run().ok()
    assert res.stdout_plain != blank_add_path


@with_files({"dummy": File("Hello World\n")})
def test_hash(nix: Nix, blank_add_path: str):
    res = nix.nix_store(["-q", "--hash", blank_add_path]).run().ok()
    hash1 = res.stdout_plain
    res = nix.nix(["--type", "sha256", "--base32", "./dummy"], "nix-hash").run().ok()
    hash2 = f"sha256:{res.stdout_plain}"
    assert hash1 == hash2
