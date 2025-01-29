#!/usr/bin/env python3

import json
import subprocess
import pytest
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any, Dict, List

TEST_ROOT = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_ROOT.parent
BIN = PROJECT_ROOT.joinpath("build", "src", "nix-eval-jobs")


def common_test(extra_args: List[str]) -> List[Dict[str, Any]]:
    with TemporaryDirectory() as tempdir:
        cmd = [str(BIN), "--gc-roots-dir", tempdir, "--meta"] + extra_args
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
        )

        results = [json.loads(r) for r in res.stdout.split("\n") if r]
        assert len(results) == 4

        built_job = results[0]
        assert built_job["attr"] == "builtJob"
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]["broken"] is False

        dotted_job = results[1]
        assert dotted_job["attr"] == '"dotted.attr"'
        assert dotted_job["attrPath"] == ["dotted.attr"]

        recurse_drv = results[2]
        assert recurse_drv["attr"] == "recurse.drvB"
        assert recurse_drv["name"] == "drvB"

        substituted_job = results[3]
        assert substituted_job["attr"] == "substitutedJob"
        assert substituted_job["name"].startswith("hello-")
        assert substituted_job["meta"]["broken"] is False
        return results


def test_flake() -> None:
    results = common_test(["--flake", ".#hydraJobs"])
    for result in results:
        assert "isCached" not in result


def test_query_cache_status() -> None:
    results = common_test(["--flake", ".#hydraJobs", "--check-cache-status"])
    # FIXME in the nix sandbox we cannot query binary caches
    # this would need some local one
    for result in results:
        assert "isCached" in result


def test_expression() -> None:
    results = common_test(["ci.nix"])
    for result in results:
        assert "isCached" not in result

    with open(TEST_ROOT.joinpath("assets/ci.nix"), "r") as ci_nix:
        common_test(["-E", ci_nix.read()])


def test_eval_error() -> None:
    with TemporaryDirectory() as tempdir:
        cmd = [
            str(BIN),
            "--gc-roots-dir",
            tempdir,
            "--meta",
            "--workers",
            "1",
            "--flake",
            ".#legacyPackages.x86_64-linux.brokenPkgs",
        ]
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            stdout=subprocess.PIPE,
        )
        print(res.stdout)
        attrs = json.loads(res.stdout)
        assert attrs["attr"] == "brokenPackage"
        assert "this is an evaluation error" in attrs["error"]


@pytest.mark.infiniterecursion
def test_recursion_error() -> None:
    with TemporaryDirectory() as tempdir:
        cmd = [
            str(BIN),
            "--gc-roots-dir",
            tempdir,
            "--meta",
            "--workers",
            "1",
            "--flake",
            ".#legacyPackages.x86_64-linux.infiniteRecursionPkgs",
        ]
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            stderr=subprocess.PIPE,
        )
        assert res.returncode == 1
        print(res.stderr)
        assert "packageWithInfiniteRecursion" in res.stderr
        assert "possible infinite recursion" in res.stderr
