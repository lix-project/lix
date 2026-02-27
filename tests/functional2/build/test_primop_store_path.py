from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import File, with_files
from testlib.fixtures.env import ManagedEnv
from pathlib import Path
import pytest

files = {
    "config.nix": get_global_asset("config.nix"),
    "default.nix": File("""
        with import ./config.nix;

        rec {
          existing = mkDerivation {
            name = "existing";
            buildCommand = ''
              echo meow > $out
            '';
          };
          reuse-existing = mkDerivation {
            name = "reuse-existing";
            buildCommand = ''
              cat ${builtins.storePath existing.outPath} > $out
            '';
          };
        }
    """),
    "flake.nix": File("""
     { outputs = _: (import ./default.nix); }
  """),
}


@with_files(files)
def test_primop_store_path_exists(nix: Nix):
    # We build the `existing` path, it's in the store now.
    nix.nix_build(["-A", "existing"]).run().ok()
    nix.nix_build(["-A", "reuse-existing"]).run().ok()


@with_files(files)
@pytest.mark.parametrize("under_pure_eval", [True, False])
def test_primop_store_path_missing(nix: Nix, under_pure_eval: bool):
    extra_options = ["--pure-eval"] if under_pure_eval else []
    # We did not build `existing` path, this should fail.
    nix.nix_build(["-A", "reuse-existing", *extra_options]).run().expect(1)


@with_files(files)
class TestPrimopStorePathSubstitutes:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix, env: ManagedEnv, files: Path):
        # We build `existing` path.
        self.output = (
            nix.nix_build([str(files / "default.nix"), "-A", "existing"]).run().ok().stdout_plain
        )
        cache_dir = env.dirs.home / "cache"
        nix.nix(
            ["copy", self.output, "--to", f"file://{cache_dir}", "--no-require-sigs"], flake=True
        ).run().ok()
        nix.clear_store()

        nix.settings.substituters = f"file://{cache_dir}"
        nix.settings.require_sigs = False

    def test_restricted_eval(self, nix: Nix, env: ManagedEnv):
        # Using -I lists it as an allowed path.
        nix.nix_build(
            [
                "-A",
                "reuse-existing",
                "--restrict-eval",
                "-I",
                f"test_file={env.dirs.home}/default.nix",
                "-I",
                f"test_file={env.dirs.home}/config.nix",
            ]
        ).run().ok()

    def test_pure_eval(self, nix: Nix):
        # nix2 cli can't load expressions from file systems in pure eval mode.
        # nix3 cli substitute in sandboxes because they have no network, where
        # it will just hard-disable all substitution. even for file:// caches.
        expr = f"""
            derivation {{
                name = "reuse-existing";
                system = "f2";
                builder = "/bin/sh";
                args = [ "-c" "echo ${{builtins.storePath "{self.output}"}} >$out" ];
            }}
        """
        nix.nix_build(["--pure-eval", "--system", "f2", "--expr", expr]).run().ok()

    def test_impure(self, nix: Nix):
        nix.nix_build(["-A", "reuse-existing"]).run().ok()
