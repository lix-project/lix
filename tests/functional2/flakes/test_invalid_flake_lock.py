from logging import Logger
from pathlib import Path
from textwrap import dedent
from functional2.testlib.fixtures.nix import Nix
import re


def test_invalid_flake_lock(nix: Nix, tmp_path: Path, logger: Logger):
    flake_dir = tmp_path / "flake"
    flake_dir.mkdir()

    (flake_dir / "flake.nix").write_text(
        dedent("""
        {
            inputs = {};
            outputs = inputs: {};
        }
    """)
    )
    (flake_dir / "flake.lock").write_text(
        dedent("""
        {
            this flake.lock is obviously invalid
        }
    """)
    )

    cmd = nix.nix(["build"], flake=True)
    cmd.cwd = flake_dir
    res = cmd.run().expect(1)
    logger.info(res.stderr_plain)

    error_re1 = re.compile(rf"while updating the lock file of flake 'path:{flake_dir}.+'")
    error_re2 = re.compile(r"while parsing the lock file at .+")
    assert error_re1.search(res.stderr_plain)
    assert error_re2.search(res.stderr_plain)
