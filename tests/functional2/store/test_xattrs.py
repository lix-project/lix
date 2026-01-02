import pytest
import sys

from testlib.fixtures.file_helper import with_files, File
from testlib.fixtures.nix import Nix
from testlib.xattrs import verify_no_xattrs_in_tree, skip_if_xattrs_are_unsupported

import xattr
import json
import re


@with_files({"file-with-xattrs": File("hi")})
@pytest.mark.parametrize("hash_fn", ["sha1", "sha256"])
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_add_xattrs_fixed_flat_file(nix: Nix, hash_fn: str):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "file-with-xattrs", "user.deriver", "test")

    res = nix.nix_store(["--add-fixed", hash_fn, "file-with-xattrs"]).run().ok()
    verify_no_xattrs_in_tree(nix.physical_store_path_for(res.stdout_plain))


@with_files({"file-with-xattrs": File("hi")})
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_add_xattrs_flat_file(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "file-with-xattrs", "user.deriver", "test")

    res = nix.nix_store(["--add", "file-with-xattrs"]).run().ok()
    verify_no_xattrs_in_tree(nix.physical_store_path_for(res.stdout_plain))


@with_files({"dir-with-xattrs": {"test1": File("hi")}})
@pytest.mark.parametrize("hash_fn", ["sha1", "sha256"])
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_add_xattrs_directories(nix: Nix, hash_fn: str):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "dir-with-xattrs", "user.deriver", "hi")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs/test1", "user.deriver", "hi")

    res = nix.nix_store(["--add-fixed", hash_fn, "--recursive", "dir-with-xattrs"]).run().ok()
    verify_no_xattrs_in_tree(nix.physical_store_path_for(res.stdout_plain))


@with_files({"file-with-xattrs": File("hi"), "dir-with-xattrs": {"test1": File("hi")}})
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_interpolating_xattrs_file(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "file-with-xattrs", "user.deriver", "test")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs", "user.deriver", "hi")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs/test1", "user.deriver", "hi")

    res = nix.eval('"${./file-with-xattrs}"', flags=["--impure"]).ok()
    store_path = json.loads(res.stdout_plain)
    verify_no_xattrs_in_tree(nix.physical_store_path_for(store_path))

    res = nix.eval('"${./dir-with-xattrs}"', flags=["--impure"]).ok()
    store_path = json.loads(res.stdout_plain)
    verify_no_xattrs_in_tree(nix.physical_store_path_for(store_path))


@with_files({"file-with-xattrs": File("hi"), "dir-with-xattrs": {"test1": File("hi")}})
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_builtins_path_xattrs(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "file-with-xattrs", "user.deriver", "test")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs", "user.deriver", "hi")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs/test1", "user.deriver", "hi")

    res = nix.eval(
        'builtins.path { name = "test-file"; path = ./file-with-xattrs; }', flags=["--impure"]
    ).ok()
    store_path = json.loads(res.stdout_plain)
    verify_no_xattrs_in_tree(nix.physical_store_path_for(store_path))

    res = nix.eval(
        'builtins.path { name = "test-file"; path = ./file-with-xattrs; recursive = true; }',
        flags=["--impure"],
    ).ok()
    store_path = json.loads(res.stdout_plain)
    verify_no_xattrs_in_tree(nix.physical_store_path_for(store_path))

    res = nix.eval(
        'builtins.path { name = "test-dir"; path = ./dir-with-xattrs; recursive = true; }',
        flags=["--impure"],
    ).ok()
    store_path = json.loads(res.stdout_plain)
    verify_no_xattrs_in_tree(nix.physical_store_path_for(store_path))


@with_files({"file-with-xattrs": File("hi"), "dir-with-xattrs": {"test1": File("hi")}})
@pytest.mark.skipif(
    sys.platform != "linux", reason="xattrs scrubbing is not expected to function outside of Linux"
)
def test_nix_prefetch_xattrs(nix: Nix):
    skip_if_xattrs_are_unsupported(nix.env)

    xattr.set(nix.env.dirs.home / "file-with-xattrs", "user.deriver", "test")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs", "user.deriver", "hi")
    xattr.set(nix.env.dirs.home / "dir-with-xattrs/test1", "user.deriver", "hi")

    locate_path_re = re.compile(r"path is '(.*)'")
    for p in ("file-with-xattrs", "dir-with-xattrs"):
        res = nix.nix_prefetch_url([f"file://{nix.env.dirs.home / p}"]).run().ok()
        matches = locate_path_re.findall(res.stderr_plain)
        assert matches, "nix prefetch must return at least one path"
        verify_no_xattrs_in_tree(nix.physical_store_path_for(matches[0]))
