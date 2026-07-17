from pathlib import Path
import shutil

import pytest

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix, NixDaemon
from testlib.utils import get_global_asset_pack


@with_files({"simple": get_global_asset_pack("simple-drv")})
class TestCopyLog:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix, files: Path):
        nix.settings.add_xp_feature("nix-command")

        # build test drv in a second store so we actually have a log to copy
        nix.nix(["build", "-f", f"{files}/simple/simple.nix"]).run().ok()
        self.drv = nix.nix_store(["--query", "--deriver", "result"]).run().ok().stdout_plain
        self.log_text = nix.nix(["log", self.drv]).run().ok().stdout_s

    @pytest.mark.no_daemon
    def test_copy_log_to_cache(self, nix: Nix):
        nix.nix(
            ["store", "copy-log", "--to", f"file://{nix.env.dirs.cache_dir}", self.drv]
        ).run().ok()
        shutil.rmtree(nix.env.dirs.nix_log_dir)
        nix.nix(["log", self.drv]).run().expect(1)
        nix.nix(
            ["store", "copy-log", "--from", f"file://{nix.env.dirs.cache_dir}", self.drv]
        ).run().ok()
        assert self.log_text == nix.nix(["log", self.drv]).run().ok().stdout_s

    def test_copy_log_untrusted(self, nix: Nix, daemon: NixDaemon):
        nix.nix(["store", "copy-log", "--to", nix.env.dirs.cache_dir, self.drv]).run().ok()
        with daemon(nix) as inner:
            cmd = inner.nix(["store", "copy-log", "--from", nix.env.dirs.cache_dir, self.drv]).run()
            cmd.expect(1)
            assert "you are not privileged to add logs" in cmd.stderr_plain

    @pytest.mark.nix_settings(trusted_users="*")
    def test_copy_log_trusted(self, nix: Nix):
        nix.nix(["store", "copy-log", "--to", nix.env.dirs.cache_dir, self.drv]).run().ok()
        shutil.rmtree(nix.env.dirs.nix_log_dir)
        nix.nix(["log", self.drv]).run().expect(1)
        nix.nix(["store", "copy-log", "--from", nix.env.dirs.cache_dir, self.drv]).run().ok()
        assert self.log_text == nix.nix(["log", self.drv]).run().ok().stdout_s
