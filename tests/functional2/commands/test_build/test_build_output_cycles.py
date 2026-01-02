import re
import sys

from testlib.fixtures.file_helper import with_files, CopyFile
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset_pack

_files = {
    "output-cycles.nix": CopyFile("assets/test_build/output-cycles.nix"),
    **get_global_asset_pack("dependencies"),
}


def _assert_cycle_tree(output: str, regexes: list[str]):
    lines = output.splitlines()
    start = next((k for k, v in enumerate(lines) if "Shown below are the files inside" in v), None)

    assert start is not None

    for line, regex in enumerate(regexes, start=start + 1):
        assert re.search(regex, lines[line])


def _assert_cycle_message(err: str):
    assert (
        len(
            re.findall(
                r"cycle detected in build of '.*' in the references of output 'bar' from output 'foo'",
                err,
            )
        )
        == 1
    )


@with_files(_files)
def test_cycle(nix: Nix):
    res = nix.nix_build(["output-cycles.nix", "-A", "cycle"]).run().expect(1)
    err = res.stderr_plain
    _assert_cycle_message(err)

    if sys.platform == "linux":
        _assert_cycle_tree(
            err,
            [
                r"/store/.*-cycle-bar",
                r"└───lib/libfoo: ….*cycle-baz.*",
                r"    →.*/store/.*-cycle-baz",
                r"    └───share/lalala:.*-cycle-foo.*",
            ],
        )


@with_files(_files)
def test_cycle_in_dependency(nix: Nix):
    res = nix.nix_build(["output-cycles.nix", "-A", "as_dependency"]).run().expect(1)
    err = res.stderr_plain
    _assert_cycle_message(err)

    assert "error: 1 dependencies of derivation" in err


@with_files(_files)
def test_cycle_with_deps(nix: Nix):
    res = nix.nix_build(["output-cycles.nix", "-A", "cycle-with-deps"]).run().expect(1)
    err = res.stderr_plain
    _assert_cycle_message(err)

    if sys.platform == "linux":
        _assert_cycle_tree(
            err,
            [
                r"/store/.*-cycle-with-deps-bar",
                r"└───txt: ….*cycle-with-deps-foo.*",
                r"    →.*/store/.*-cycle-with-deps-foo",
                r"    └───txt:.*-cycle-with-deps-bar.*",
            ],
        )
