from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset, CopyTemplate
from testlib.environ import environ
import pytest


@with_files(
    {
        "shell-hello.nix": get_global_asset("shell-hello.nix"),
        "config.nix": get_global_asset("config.nix"),
        "flake.nix": CopyTemplate("assets/run/flake.nix", {"system": environ.get("system")}),
    }
)
class TestFlakeRun:
    @pytest.mark.parametrize("attr", ["appAsApp", "pkgAsPkg"])
    def test_run_succeeds(self, nix: Nix, attr: str):
        assert (
            "Hello World"
            in nix.nix(["run", "--no-write-lock-file", f".#{attr}"], flake=True).run().ok().stdout_s
        )

    def test_run_pkg_as_app(self, nix: Nix):
        assert (
            "should have type 'derivation'"
            in nix.nix(["run", "--no-write-lock-file", ".#pkgAsApp"], flake=True)
            .run()
            .expect(1)
            .stderr_s
        )

    def test_run_app_as_pkg(self, nix: Nix):
        assert (
            "should have type 'app'"
            in nix.nix(["run", "--no-write-lock-file", ".#appAsPkg"], flake=True)
            .run()
            .expect(1)
            .stderr_s
        )
