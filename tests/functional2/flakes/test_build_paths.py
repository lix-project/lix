from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, File
from testlib.fixtures.command import CommandResult
from pathlib import Path
from .common import simple_flake
import tarfile
import pytest

flake_nix = """
{
  inputs.flake2.url = "file://$TEST_ROOT/flake.tar.gz";

  outputs = { self, flake2 }: {

    a1 = builtins.fetchTarball {
      #type = "tarball";
      url = "file://$TEST_ROOT/flake.tar.gz";
      sha256 = "$hash";
    };

    a2 = ./foo;

    a3 = ./.;

    a4 = self.outPath;

    # FIXME
    a5 = self;

    a6 = flake2.outPath;

    # FIXME
    a7 = "${flake2}/config.nix";

    # This is only allowed in impure mode.
    a8 = builtins.storePath $dep;

    a9 = "$dep";

    drvCall = with import ./config.nix; mkDerivation {
      name = "simple";
      builder = ./simple.builder.sh;
      PATH = "";
      goodPath = path;
    };

    a10 = builtins.unsafeDiscardOutputDependency self.drvCall.drvPath;

    a11 = self.drvCall.drvPath;

    a12 = self.drvCall.outPath;

    a13 = "${self.drvCall.drvPath}${self.drvCall.outPath}";
  };
}
"""

files = {
    "flake1": simple_flake() | {"foo": File("bar")},
    "flake2": simple_flake(),
    "common.sh": File("nothing important"),
}


@with_files(files)
class TestBuild:
    @pytest.fixture(autouse=True)
    def common_init(self, nix: Nix, files: Path):
        self.nix = nix
        self.out = nix.env.dirs.home / "result"

        nix.settings.add_xp_feature("nix-command", "flakes")

        with tarfile.open(files / "flake.tar.gz", "w:gz") as tar:

            def drop_prefix(ti: tarfile.TarInfo) -> tarfile.TarInfo:
                ti.name = ti.name.replace(str(files)[1:] + "/", "")
                return ti

            tar.add(files / "flake2", filter=drop_prefix)

        flake_hash = nix.hash_path(files / "flake2")
        dep = nix.nix(["store", "add-path", files / "common.sh"]).run().ok().stdout_s.strip()

        flake = files / "flake1/flake.nix"
        flake.write_text(
            flake_nix.replace("$TEST_ROOT", str(files))
            .replace("$hash", flake_hash)
            .replace("$dep", dep)
        )

    def build(self, *args) -> CommandResult:
        return self.nix.nix(["build", "--json", "--out-link", self.out, *args]).run()

    @pytest.mark.parametrize("attr", ["a1", "a6"])
    def test_build_simple_nix(self, files: Path, attr: str):
        self.build(f"{files}/flake1#{attr}").ok()
        assert (self.out / "simple.nix").exists()

    def test_build_a2(self, files: Path):
        self.build(f"{files}/flake1#a2").ok()
        assert self.out.read_text() == "bar"

    @pytest.mark.parametrize("attr", ["a3", "a4"])
    def test_build_plain(self, files: Path, attr: str):
        self.build(f"{files}/flake1#{attr}").ok()

    def test_build_a8(self, files: Path):
        self.build("--impure", f"{files}/flake1#a8").ok()
        assert self.out.read_text() == "nothing important"

    def test_build_a9(self, files: Path):
        result = self.build(f"{files}/flake1#a9").expect(1)
        assert (
            "has 0 entries in its context. It should only have exactly one entry" in result.stderr_s
        )

    def test_build_a10(self, files: Path):
        self.build(f"{files}/flake1#a10").ok()
        assert str(self.out.readlink()).endswith("simple.drv")

    def test_build_a11(self, files: Path):
        result = self.build(f"{files}/flake1#a11").expect(1)
        assert (
            "has a context which refers to a complete source and binary closure" in result.stderr_s
        )

    def test_build_a12(self, files: Path):
        self.build(f"{files}/flake1#a12").ok()
        assert (self.out / "hello").exists()

    def test_build_a13(self, files: Path):
        result = self.build("--impure", f"{files}/flake1#a13").expect(1)
        assert (
            "has 2 entries in its context. It should only have exactly one entry" in result.stderr_s
        )
