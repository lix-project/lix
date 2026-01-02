from collections.abc import Callable
from pathlib import Path

from lang.test_lang import test_eval_okay as nix_eval
from testlib.fixtures.file_helper import with_files, File, AssetSymlink
from testlib.fixtures.nix import Nix
from testlib.fixtures.snapshot import Snapshot


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
