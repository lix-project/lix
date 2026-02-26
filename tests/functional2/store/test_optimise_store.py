from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


@with_files({"config.nix": get_global_asset("config.nix")})
class TestOptimizeStore:
    def _test_optimise_store(self, nix: Nix):
        def build(name: str, *extra_args: str) -> Path:
            expr = f"""
                with import ./config.nix; mkDerivation {{
                    name = "{name}";
                    builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo";
                }}
            """

            result = nix.nix_build(["-E", expr, "--no-out-link", *extra_args]).run().ok()
            return Path(result.stdout_plain)

        out1 = build("foo1", "--auto-optimise-store")
        out2 = build("foo2", "--auto-optimise-store")

        assert (out1 / "foo").samefile(out2 / "foo")
        assert (out1 / "foo").stat().st_nlink == 3

        out3 = build("foo3", "--no-auto-optimise-store")

        assert not (out1 / "foo").samefile(out3 / "foo")

        nix.nix_store(["--optimise"]).run().ok()

        assert (out1 / "foo").samefile(out3 / "foo")

        nix.nix_store(["--gc"]).run().ok()

        assert list((nix.env.dirs.real_store_dir / ".links").glob("*")) == []

    def test_optimise_store(self, nix: Nix):
        self._test_optimise_store(nix)

    def test_optimise_store_daemon(self, nix: Nix):
        nix.settings.auto_optimise_store = True
        with nix.daemon([], {"trusted-users": "*"}) as inner:
            self._test_optimise_store(inner)
