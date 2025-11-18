from collections.abc import Callable
from pathlib import Path

from functional2.lang.test_lang import test_eval as nix_eval
from functional2.testlib.fixtures.file_helper import with_files, File, AssetSymlink
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.fixtures.snapshot import Snapshot


@with_files(
    {
        "in.nix": File(
            'builtins.getEnv "TEST_VAR" + (if builtins.getEnv "NO_SUCH_VAR" == "" then "bar" else "bla")'
        ),
        "out.exp": AssetSymlink("eval-okay.out.exp"),
        "err.exp": AssetSymlink("eval-okay.err.exp"),
    }
)
def test_get_env(nix: Nix, snapshot: Callable[[str], Snapshot], files: Path):
    nix.env["TEST_VAR"] = "foo"
    nix_eval(files, nix, [], snapshot)
