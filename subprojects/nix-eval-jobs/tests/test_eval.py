#!/usr/bin/env python3

import json
import os
import subprocess
import pytest
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any, Dict, List

TEST_ROOT = Path(__file__).parent.resolve()
# subprojects/nix-eval-jobs/tests
# PROJECT_ROOT = TEST_ROOT.parent.parent.parent
# BIN = PROJECT_ROOT.joinpath("outputs", "out", "bin", "nix-eval-jobs")
BIN = "nix-eval-jobs"


def check_gc_root(gcRootDir: str, drvPath: str):
    """
    Make sure the expected GC root exists in the given dir
    """
    link_name = os.path.basename(drvPath)
    symlink_path = os.path.join(gcRootDir, link_name)
    assert os.path.islink(symlink_path) and drvPath == os.readlink(symlink_path)


def evaluate(
    tempdir: TemporaryDirectory,
    expected_statuscode: int = 0,
    extra_args: List[str] = [],
) -> tuple[Dict[str, Dict[str, Any]], str]:
    if nixpkgs_path := os.getenv("NEJ_NIXPKGS_PATH"):
        if "--flake" in extra_args:
            extra_args.extend(["--override-input", "nixpkgs", f"path:{nixpkgs_path}"])
        else:
            extra_args.extend(["--arg", "pkgs", f"import {nixpkgs_path} {{}}"])

    cmd = [
        str(BIN),
        "--gc-roots-dir",
        tempdir,
        "--meta",
        "--extra-experimental-features",
        "flakes",
    ] + extra_args
    res = subprocess.run(
        cmd,
        cwd=TEST_ROOT.joinpath("assets"),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    assert res.returncode == expected_statuscode
    print(res.stdout)
    print(res.stderr)
    return [json.loads(r) for r in res.stdout.split("\n") if r], res.stderr


def common_test(extra_args: List[str]) -> List[Dict[str, Any]]:
    with TemporaryDirectory() as tempdir:
        results, _ = evaluate(tempdir, 0, extra_args)
        assert len(results) == 4

        built_job = results[0]
        assert built_job["attr"] == "builtJob"
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]["broken"] is False
        check_gc_root(tempdir, built_job['drvPath'])

        dotted_job = results[1]
        assert dotted_job["attr"] == '"dotted.attr"'
        assert dotted_job["attrPath"] == ["dotted.attr"]
        check_gc_root(tempdir, dotted_job['drvPath'])

        recurse_drv = results[2]
        assert recurse_drv["attr"] == "recurse.drvB"
        assert recurse_drv["name"] == "drvB"
        check_gc_root(tempdir, recurse_drv['drvPath'])

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
        results, _ = evaluate(
            tempdir,
            0,
            ["--workers", "1", "--flake", ".#legacyPackages.x86_64-linux.brokenPkgs"],
        )
        assert len(results) == 1

        attr = results[0]
        assert attr["attr"] == "brokenPackage"
        assert "this is an evaluation error" in attr["error"]


@pytest.mark.infiniterecursion
def test_recursion_error() -> None:
    with TemporaryDirectory() as tempdir:
        results, stderr = evaluate(
            tempdir,
            1,
            [
                "--workers",
                "1",
                "--flake",
                ".#legacyPackages.x86_64-linux.infiniteRecursionPkgs",
            ],
        )
        print(stderr)
        assert "packageWithInfiniteRecursion" in stderr
        assert "possible infinite recursion" in stderr
