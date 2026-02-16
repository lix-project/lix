import json
from pathlib import Path
from typing import Concatenate
from collections.abc import Callable

import pytest

from testlib.fixtures.file_helper import with_files, File, AssetSymlink, Symlink
from testlib.fixtures.nix import Nix
from testlib.fixtures.command import Command


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


@with_files({"dummy": File("Hello World\n"), "item2": File("foo bar")})
class TestNix3AddPath:
    @pytest.fixture(autouse=True)
    def enable_flakes(self, nix: Nix):
        nix.settings.add_xp_feature("nix-command", "flakes")

    # type checkers hate this one weird trick
    AddPath = Callable[Concatenate[str, ...], Command]

    @pytest.fixture
    def add_path(self, nix: Nix) -> AddPath:
        def inner(name: str, references_list: str | None = None) -> Command:
            references_list_arg = (
                ["--references-list-json", references_list] if references_list else []
            )
            return nix.nix(["store", "add-path", *references_list_arg, name])

        return inner

    def test_nix3_rec_basic(self, add_path: AddPath, blank_add_path: str):
        res = add_path("./dummy").run().ok().stdout_plain
        assert res == blank_add_path

    def test_nix3_rec_empty_references(self, files: Path, add_path: AddPath, blank_add_path: str):
        # no references
        (files / "reflist.json").write_text("[]")

        path = add_path("./dummy", "reflist.json").run().ok().stdout_plain
        assert path == blank_add_path

    def test_nix3_rec_bad_json(self, files: Path, add_path: AddPath):
        (files / "reflist.json").write_text("parse error")

        err = add_path("./dummy", "reflist.json").run().expect(1).stderr_plain
        assert "references list file" in err

    def test_nix3_rec_some_references(
        self, files: Path, nix: Nix, add_path: AddPath, blank_add_path: str
    ):
        path2 = add_path("item2").run().ok().stdout_plain

        # some references, which should cause a different output path
        (files / "reflist.json").write_text(json.dumps([blank_add_path, path2]))

        path = add_path("./dummy", "reflist.json").run().ok().stdout_plain
        assert path != blank_add_path

        # and the resulting path in the store also has the right references
        out = nix.nix(["path-info", "--json", path]).run().ok().stdout_s
        out = json.loads(out)
        assert set(out[0]["references"]) == {blank_add_path, path2}

    def test_nix3_rec_bad_json_type(self, files: Path, add_path: AddPath):
        (files / "reflist.json").write_text("{}")

        err = add_path("./dummy", "reflist.json").run().expect(1).stderr_plain

        assert "type must be array" in err
