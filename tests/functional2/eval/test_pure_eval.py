from pathlib import Path

import pytest

from functional2.testlib.fixtures.file_helper import with_files, File, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset


use_impure_message = "is forbidden in pure eval mode (use '--impure' to override)"


def test_missing_impure_flag(nix: Nix):
    res = nix.eval("builtins.readFile ./test_pure_eval.py").expect(1)
    assert use_impure_message in res.stderr_s


def test_unauthorized_paths(nix: Nix):
    res = nix.eval("builtins.pathExists ./test_pure_eval.py").json()
    assert res is False, "Calling 'pathExists' on a non-authorised path should return false"


def test_builtins_time_not_available(nix: Nix):
    res = nix.eval("builtins.currentTime").expect(1)
    assert "error: attribute 'currentTime' missing" in res.stderr_s


def test_builtins_system_not_available(nix: Nix):
    res = nix.eval("builtins.currentSystem").expect(1)
    assert "error: attribute 'currentSystem' missing" in res.stderr_s


@with_files({"trivial.nix": get_global_asset("trivial.nix")})
def test_pure_eval_simple_not_working(nix: Nix):
    res = nix.nix_instantiate(["--pure-eval", "trivial.nix"]).run().expect(1)
    assert use_impure_message in res.stderr_s


def test_readdir_not_allowed(nix: Nix):
    res = nix.eval('builtins.readDir "/"').expect(1)
    assert use_impure_message in res.stderr_s


@with_files({"test-file": File("bäh-sh")})
def test_impure_allows_readfile(nix: Nix):
    res = (
        nix.nix(["eval", "--impure", "--expr", "builtins.readFile ./test-file"], flake=True)
        .run()
        .ok()
    )
    assert res.stdout_plain == '"bäh-sh"'


@pytest.fixture
def fetch_url_expr(files: Path) -> str:
    return f'(import (builtins.fetchurl {{ url = "file://{files}/pure-eval.nix"; }})).x'


@with_files({"pure-eval.nix": CopyFile("assets/pure-eval.nix")})
def test_impure_allows_fetchurl(nix: Nix, fetch_url_expr: str):
    res = nix.nix(["eval", "--impure", "--expr", fetch_url_expr], flake=True).run().ok()
    assert res.stdout_plain == "123"


@with_files({"pure-eval.nix": CopyFile("assets/pure-eval.nix")})
def test_fetchurl_requires_hash(nix: Nix, fetch_url_expr: str):
    res = nix.nix(["eval", "--expr", fetch_url_expr], flake=True).run().expect(1)
    assert "error: in pure evaluation mode, 'fetchurl' requires a 'sha256'" in res.stderr_s


@with_files({"pure-eval.nix": CopyFile("assets/pure-eval.nix")})
def test_fetchurl_available_with_hash(nix: Nix, files: Path):
    expected_hash = "sha256-YXhmEC+QvjpS/+wE8kE0hqpu5w+A7jhIfqCLpRVpw8w="
    nix.eval(
        f'(import (builtins.fetchurl {{ url = "file://{files}/pure-eval.nix"; sha256 = "{expected_hash}"; }})).x'
    )


def test_eval_no_resolve_in_pure(nix: Nix):
    res = nix.eval("~/foo").expect(1)
    assert "can not be resolved in pure mode" in res.stderr_plain
