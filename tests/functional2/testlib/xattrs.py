import xattr
import pytest

from pathlib import Path
from functional2.testlib.fixtures.env import ManagedEnv


def has_xattrs(path: Path) -> bool:
    """
    Check if a file or directory has any extended attributes.
    """
    try:
        xattrs_list = xattr.list(str(path), nofollow=True)
        return bool(xattrs_list)
    except OSError:
        # If an error occurs, either because the file doesn't exist or no xattrs, return False
        return False


def verify_no_xattrs_in_tree(root_dir: Path) -> None:
    """
    Traverse a directory tree and verify no file or directory has xattrs.
    """
    if root_dir.is_file():
        assert not has_xattrs(root_dir)
        return

    for entry in root_dir.iterdir():
        assert not has_xattrs(entry)
        if entry.is_dir():
            verify_no_xattrs_in_tree(entry)


def test_set_clear_xattrs_in(dir_: Path) -> bool:
    try:
        xattr.set(dir_, "user.test", "1", nofollow=True)
        xattr.remove(dir_, "user.test", nofollow=True)
        return True
    except OSError:
        return False


def skip_if_xattrs_are_unsupported(env: ManagedEnv) -> None:
    """
    Skip the current test if xattrs are unsupported in, either:
    - the test root
    - the fixture root
    """
    if not test_set_clear_xattrs_in(env.dirs.test_root):
        pytest.skip("xattrs cannot be used in the test root")

    if not test_set_clear_xattrs_in(env.dirs.home):
        pytest.skip("xattrs cannot be used in the fixture directory")
