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
        cmd = [str(BIN), "--gc-roots-dir", tempdir] + extra_args
        res = subprocess.run(
            cmd,
            cwd=TEST_ROOT.joinpath("assets"),
            text=True,
            check=True,
            stdout=subprocess.PIPE,
        )
        data = json.loads(res.stdout)

        assert data["builtJob"]["nixName"] == "job1"
        assert "out" in data["builtJob"]["outputs"]

        assert data["substitutedJob"]["nixName"].startswith("hello-")


def test_flake() -> None:
    common_test(["--flake", ".#"])


def test_expression() -> None:
    common_test(["ci.nix"])
