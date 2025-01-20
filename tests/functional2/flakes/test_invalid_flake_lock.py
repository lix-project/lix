from pathlib import Path
from textwrap import dedent
from functional2.testlib.fixtures import Nix
import re

def test_invalid_flake_lock(nix: Nix, tmp_path: Path):
    flake_dir = tmp_path / 'flake'
    flake_dir.mkdir()

    (flake_dir / 'flake.nix').write_text(dedent("""
        {
            inputs = {};
            outputs = inputs: {};
        }
    """))
    (flake_dir / 'flake.lock').write_text(dedent("""
        {
            this flake.lock is obviously invalid
        }
    """))

    cmd = nix.nix(["build"], flake=True)
    cmd.cwd = flake_dir
    res = cmd.run().expect(1)
    print(res.stderr_plain)

    ERROR_RE1 = re.compile(fr"while updating the lock file of flake 'path:{flake_dir}.+'")
    ERROR_RE2 = re.compile(fr"while parsing the lock file at .+")
    assert ERROR_RE1.search(res.stderr_plain)
    assert ERROR_RE2.search(res.stderr_plain)
