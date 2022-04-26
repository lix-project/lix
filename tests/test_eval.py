#!/usr/bin/env python3

import subprocess
import json
from tempfile import TemporaryDirectory
from pathlib import Path
from typing import List

TEST_ROOT = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_ROOT.parent
BIN = PROJECT_ROOT.joinpath("build", "src", "nix-eval-jobs")


def common_test(extra_args: List[str]) -> None:
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
        assert len(results) == 5

        built_job = results[0]
        assert built_job["attr"] == "builtJob"
        assert built_job["name"] == "job1"
        assert built_job["outputs"]["out"].startswith("/nix/store")
        assert built_job["drvPath"].endswith(".drv")
        assert built_job["meta"]['broken'] is False

        dotted_job = results[1]
        assert dotted_job["attr"] == "\"dotted.attr\""
        assert dotted_job["attrPath"] == [ "dotted.attr" ]

        recurse_drv = results[2]
        assert recurse_drv["attr"] == "recurse.drvB"
        assert recurse_drv["name"] == "drvB"

        recurse_recurse_bool = results[3]
        assert "error" in recurse_recurse_bool

        substituted_job = results[4]
        assert substituted_job["attr"] == "substitutedJob"
        assert substituted_job["name"].startswith("hello-")
        assert substituted_job["meta"]['broken'] is False


def test_flake() -> None:
    common_test(["--flake", ".#hydraJobs"])


def test_expression() -> None:
    common_test(["ci.nix"])
