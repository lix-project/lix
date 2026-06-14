import pytest
from pathlib import Path
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import CopyFile
from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import with_files
import re

check_req_files = {
    "check-reqs.nix": CopyFile("assets/check-reqs.nix"),
    "config.nix": get_global_asset("config.nix"),
}


@with_files(check_req_files)
@pytest.mark.parametrize("attr", ["test1", "test7", "test9"])
def test_good(nix: Nix, files: Path, attr: str):
    result = files / "result"
    nix.nix_build(["-o", result, "check-reqs.nix", "-A", attr]).run().ok()

    assert result.exists()


@with_files(check_req_files)
@pytest.mark.parametrize(
    "attr",
    [
        "test2",
        "test3",
        "test5",
        "test8",  # ignoreSelfRefs is only true for drvs using structuredAttrs.
    ],
)
def test_bad(nix: Nix, files: Path, attr: str):
    res = nix.nix_build(["-o", files / "result", "check-reqs.nix", "-A", attr]).run().expect(1)

    assert "Shown below are chains that lead to the forbidden path(s)." in res.stderr_plain
    assert "is not allowed to refer to the following paths:" in res.stderr_plain


@with_files(check_req_files)
@pytest.mark.parametrize(
    ("attr", "bad_refs"),
    [
        ("test4", ["check-reqs-dep1", "check-reqs-dep2"]),
        ("test6", ["check-reqs-dep1", "check-reqs-dep2", "└───.*/.*-check-reqs-deps"]),
    ],
)
def test_bad_deps(nix: Nix, files: Path, attr: str, bad_refs: list[str]):
    res = nix.nix_build(["-o", files / "result", "check-reqs.nix", "-A", attr]).run().expect(1)
    err = res.stderr_plain

    assert "Shown below are chains that lead to the forbidden path(s)." in err
    assert "is not allowed to refer to the following paths:" in err

    for bad_ref in bad_refs:
        assert re.search(bad_ref, err)
