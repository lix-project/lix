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


def test_constituents() -> None:
    with TemporaryDirectory() as tempdir:
        results, _ = evaluate(
            tempdir,
            0,
            [
                "--workers",
                "1",
                "--flake",
                ".#legacyPackages.x86_64-linux.constituents.success",
                "--constituents",
            ],
        )
        assert len(results) == 4

        child = results[0]
        assert child["attr"] == "anotherone"
        assert "constituents" not in child
        assert "namedConstituents" not in child

        direct = results[1]
        assert direct["attr"] == "direct_aggregate"
        assert "constituents" in direct
        assert "namedConstituents" not in direct

        indirect = results[2]
        assert indirect["attr"] == "indirect_aggregate"
        assert "constituents" in indirect
        assert "namedConstituents" not in indirect

        mixed = results[3]
        assert mixed["attr"] == "mixed_aggregate"

        def absent_or_empty(f: str, d: dict) -> bool:
            return f not in d or len(d[f]) == 0

        assert absent_or_empty("namedConstituents", direct)
        assert absent_or_empty("namedConstituents", indirect)
        assert absent_or_empty("namedConstituents", mixed)

        assert direct["constituents"][0].endswith("-job1.drv")

        assert indirect["constituents"][0] == child["drvPath"]

        assert mixed["constituents"][0].endswith("-job1.drv")
        assert mixed["constituents"][1] == child["drvPath"]

        assert "error" not in direct
        assert "error" not in indirect
        assert "error" not in mixed

        check_gc_root(tempdir, direct["drvPath"])
        check_gc_root(tempdir, indirect["drvPath"])
        check_gc_root(tempdir, mixed["drvPath"])


def test_constituents_cycle() -> None:
    with TemporaryDirectory() as tempdir:
        results, _ = evaluate(
            tempdir,
            0,
            [
                "--workers",
                "1",
                "--flake",
                ".#legacyPackages.x86_64-linux.constituents.cycle",
                "--constituents",
            ],
        )
        assert len(results) == 2

        assert list(map(lambda x: x["name"], results)) == ["aggregate0", "aggregate1"]
        for i in results:
            assert i["error"] == "Dependency cycle: aggregate0 <-> aggregate1"


def test_constituents_error() -> None:
    with TemporaryDirectory() as tempdir:
        results, _ = evaluate(
            tempdir,
            0,
            [
                "--workers",
                "1",
                "--flake",
                ".#legacyPackages.x86_64-linux.constituents.failures",
                "--constituents",
            ],
        )
        assert len(results) == 2

        child = results[0]
        assert child["attr"] == "doesnteval"
        assert "error" in child

        aggregate = results[1]
        assert aggregate["attr"] == "aggregate"
        assert "namedConstituents" not in aggregate
        assert "doesntexist: does not exist\n" in aggregate["error"]
        assert "constituents" in aggregate


def test_transitivity() -> None:
    with TemporaryDirectory() as tempdir:
        results, _ = evaluate(
            tempdir,
            0,
            [
                "--workers",
                "1",
                "--flake",
                ".#legacyPackages.x86_64-linux.constituents.transitive",
                "--constituents",
            ],
        )
        assert len(results) == 3

        job = results[0]
        assert job["attr"] == "constituent"
        assert "constituents" not in job

        aggregate1 = results[1]
        assert aggregate1["attr"] == "aggregate1"

        aggregate0 = results[2]
        assert aggregate0["attr"] == "aggregate0"

        assert aggregate1["drvPath"] == aggregate0["constituents"][0]
