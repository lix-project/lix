import json
import re

import pytest

from functional2.testlib.fixtures.file_helper import CopyFile, with_files
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset

_files = {
    "search.nix": CopyFile("assets/test_search/search.nix"),
    "config.nix": get_global_asset("config.nix"),
}
_exclude_args = ["search", "-f", "search.nix"]
_search_args = [*_exclude_args, ""]


@pytest.fixture(autouse=True)
def commands(nix: Nix):
    nix.settings.feature("nix-command")


@with_files(_files)
def test_pkg_name(nix: Nix):
    res = nix.nix([*_search_args, "hello"]).run().ok()
    assert len(res.stdout_plain.splitlines()) > 1


@with_files(_files)
def test_description(nix: Nix):
    res = nix.nix([*_search_args, "broken"]).run().ok()
    assert len(res.stdout_plain.splitlines()) > 1


@with_files(_files)
def test_no_exist(nix: Nix):
    res = nix.nix([*_search_args, "nosuchpackageexits"]).run().expect(1)
    assert "error: no results for the given search" in res.stderr_plain


@with_files(_files)
def test_multiple_args_exists(nix: Nix):
    res = nix.nix([*_search_args, "hello", "empty"]).run().ok()
    assert len(res.stdout_plain.splitlines()) == 2


@with_files(_files)
def test_multiple_args_no_exist(nix: Nix):
    res = nix.nix([*_search_args, "hello", "broken"]).run().expect(1)
    assert "error: no results for the given search" in res.stderr_plain


@with_files(_files)
def test_no_args(nix: Nix):
    res = nix.nix([*_search_args]).run().expect(1)
    assert "error: Must provide at least one regex!" in res.stderr_plain


@with_files(_files)
def test_match_all(nix: Nix):
    res = nix.nix([*_search_args, "^"]).run().ok()
    out = res.stdout_plain
    for pkg in ["foo", "bar", "hello"]:
        assert pkg in out


# FIXME(Jade): possibly not test this with colour in the future
@with_files(_files)
def test_overlap_1(nix: Nix):
    nix.env.set_env("FORCE_COLOR", "1")
    res = nix.nix([*_search_args, "oo", "foo", "oo"]).run().ok()
    out = res.stdout
    assert out == b"* \x1b[0;1m\x1b[32;1mfoo\x1b[0;1m\x1b[0m (5)\n"


@with_files(_files)
def test_overlap_2(nix: Nix):
    nix.env.set_env("FORCE_COLOR", "1")
    res = nix.nix([*_search_args, "broken b", "en bar"]).run().ok()
    out = res.stdout
    assert out == b"* \x1b[0;1mbar\x1b[0m (3)\n  \x1b[32;1mbroken bar\x1b[0m\n"


@with_files(_files)
def test_multiple_finds_o(nix: Nix):
    """Searching for 'o' should yield the 'o' in 'broken bar', the 'oo' in foo and 'o' in hello"""
    nix.env.set_env("FORCE_COLOR", "1")
    res = nix.nix([*_search_args, "o"]).run().ok()
    out = res.stdout_s
    assert len(re.findall(r"\x1b\[32;1mo{1,2}\x1b\[(0|0;1)m", out)) == 3


@with_files(_files)
def test_multiple_finds_b(nix: Nix):
    """Searching for 'b' should yield the 'b' in bar and the two 'b's in 'broken bar'"""
    nix.env.set_env("FORCE_COLOR", "1")
    res = nix.nix([*_search_args, "b"]).run().ok()
    out = res.stdout_s
    assert len(re.findall(r"\x1b\[32;1mb\x1b\[(0|0;1)m", out)) == 3


@with_files(_files)
def test_exclude_hello(nix: Nix):
    res = nix.nix([*_exclude_args, "", "^", "-e", "hello"]).run().ok()
    assert res.stdout_plain.count("hello") == 0


@with_files(_files)
def test_exclude_foo_regex(nix: Nix):
    res = nix.nix([*_exclude_args, "foo", "^", "--exclude", "foo|bar"]).run().expect(1)
    assert "error: no results for the given search" in res.stderr_plain


@with_files(_files)
def test_exclude_foo_multiple_args(nix: Nix):
    res = nix.nix([*_exclude_args, "foo", "^", "-e", "foo", "--exclude", "bar"]).run().expect(1)
    assert "error: no results for the given search" in res.stderr_plain


@with_files(_files)
def test_exclude_bar(nix: Nix):
    res = nix.nix([*_exclude_args, "", "^", "-e", "bar", "--json"]).run().ok()
    assert json.loads(res.stdout_plain).keys() == {"foo", "hello"}
