from pathlib import Path
import pytest

from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import File, with_files


@with_files({"example.txt": File("example")})
class TestLocalStore:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix, files: Path):  # noqa: ARG002
        nix.settings.add_xp_feature("nix-command")

        store = nix.env.dirs.home / "x"
        store.mkdir()
        nix.env.dirs.nix_store_dir = store
        nix.env.dirs.real_store_dir = store

        self.path = (
            nix.nix_store(["--store", "./x", "--add", "example.txt"]).run().ok().stdout_plain
        )

    def test_path_info_relative(self, nix: Nix):
        store = "./x"
        path = nix.nix(["path-info", "--store", store, self.path]).run().ok().stdout_plain
        assert path == self.path

    def test_path_info_absolute(self, nix: Nix):
        store = f"{nix.env.dirs.home}/x"
        path = nix.nix(["path-info", "--store", store, self.path]).run().ok().stdout_plain
        assert path == self.path

    def test_path_info_uri(self, nix: Nix):
        store = f"local?root={nix.env.dirs.home}/x"
        path = nix.nix(["path-info", "--store", store, self.path]).run().ok().stdout_plain
        assert path == self.path

    def test_local_is_trusted(self, nix: Nix):
        info = nix.nix(["--store", "./x", "store", "ping", "--json"]).run().json()
        assert info["trusted"]

    def test_doctor_shows_trust(self, nix: Nix):
        result = nix.nix(["--store", "./x", "doctor"]).run().ok()
        assert "You are trusted by" in result.stderr_plain
