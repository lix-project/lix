from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.environ import environ
from testlib.utils import get_global_asset_pack, get_global_asset
from pathlib import Path
import pytest

system = environ.get("system")

files = {
    "templates": get_global_asset_pack(".git")
    | {
        "flake.nix": File("""{
          description = "Some templates";

          outputs = { self }: {
            templates = rec {
              trivial = {
                path = ./trivial;
                description = "A trivial flake";
                welcomeText = ''
                    Welcome to my trivial flake
                '';
              };
              default = trivial;
            };
          };
        }"""),
        "trivial": {
            "flake.nix": File(f"""{{
              description = "A flake for building Hello World";

              outputs = {{ self, nixpkgs }}: {{
                packages.{system} = rec {{
                  hello = nixpkgs.legacyPackages.{system}.hello;
                  default = hello;
                }};
              }};
            }}"""),
            "a": File("a"),
            "b": File("b"),
        },
    },
    "flake": get_global_asset_pack(".git"),
    "nixpkgs": get_global_asset_pack(".git")
    | {
        "config.nix": get_global_asset("config.nix"),
        "flake.nix": File(f"""{{
          outputs = {{ self }}: {{
            legacyPackages.{system}.hello =
              with import ./config.nix;
              mkDerivation {{
                name = "hello";
                buildCommand = "echo hello > $out";
              }};
          }};
        }}"""),
    },
    "registry.json": File(""),
}


@pytest.fixture(autouse=True)
def common_init(nix: Nix, files: Path, git: Git):
    nix.settings.add_xp_feature("nix-command", "flakes")
    nix.settings["flake-registry"] = str(files / "registry.json")

    git(files / "templates", "add", "flake.nix", "trivial/")
    git(files / "templates", "commit", "-m", "Initial")
    git(files / "nixpkgs", "add", "flake.nix", "config.nix")

    nix.nix(
        [
            "registry",
            "add",
            "--registry",
            files / "registry.json",
            "templates",
            f"git+file://{files}/templates",
        ]
    ).run().ok()
    nix.nix(
        [
            "registry",
            "add",
            "--registry",
            files / "registry.json",
            "nixpkgs",
            f"git+file://{files}/nixpkgs",
        ]
    ).run().ok()


@with_files(files)
class TestFlakeTemplates:
    def test_check(self, nix: Nix, files: Path):
        assert (
            "checking template 'templates.trivial'"
            in nix.nix(["flake", "check", files / "templates"]).run().ok().stderr_s
        )

    def test_show(self, nix: Nix, files: Path):
        assert (
            "A trivial flake" in nix.nix(["flake", "show", files / "templates"]).run().ok().stdout_s
        )

    def test_show_json(self, nix: Nix, files: Path):
        result = nix.nix(["flake", "show", files / "templates", "--json"]).run().json()
        assert result["templates"]["default"]["description"] == "A trivial flake"

    def test_init(self, nix: Nix, files: Path):
        stderr = nix.nix(["flake", "init"], cwd=files / "flake").run().ok().stderr_s
        assert f"wrote: {files}/flake/flake.nix" in stderr

    class TestWithInit:
        @pytest.fixture(autouse=True)
        def init_flake(self, nix: Nix, files: Path):
            nix.nix(["flake", "init"], cwd=files / "flake").run().ok()

        def test_init_idempotent(self, nix: Nix, files: Path):
            stderr = nix.nix(["flake", "init"], cwd=files / "flake").run().ok().stderr_s
            assert "skipping identical file" in stderr

        class TestInRepo:
            @pytest.fixture(autouse=True)
            def init_repo(self, files: Path, git: Git):
                git(files / "flake", "add", "flake.nix")

            def test_check(self, nix: Nix, files: Path):
                stderr = nix.nix(["flake", "check", files / "flake"]).run().ok().stderr_s
                assert f"fetching git input 'git+file://{files}/flake'" in stderr
                assert f"fetching git input 'git+file://{files}/nixpkgs'" in stderr
                assert "evaluating flake" in stderr

            def test_show(self, nix: Nix, files: Path):
                stdout = nix.nix(["flake", "show", files / "flake"]).run().ok().stdout_s
                assert "hello: package 'hello'" in stdout

            def test_show_json(self, nix: Nix, files: Path):
                result = nix.nix(["flake", "show", files / "flake", "--json"]).run().json()
                assert result["packages"][system]["default"]["name"] == "hello"
                assert result["packages"][system]["hello"]["name"] == "hello"

    def test_benign_conflict_idempotence(self, nix: Nix, files: Path):
        (files / "flake/a").write_text("a")

        stderr = nix.nix(["flake", "init"], cwd=files / "flake").run().ok().stderr_s
        assert "skipping identical file" in stderr
        assert f"wrote: {files}/flake/flake.nix" in stderr

    def test_init_conflict(self, nix: Nix, files: Path):
        (files / "flake/a").write_text("b")

        stderr = nix.nix(["flake", "init"], cwd=files / "flake").run().expect(1).stderr_s
        assert f"refusing to overwrite existing file '{files}/flake/a'" in stderr

    def test_new(self, nix: Nix):
        stderr = nix.nix(["flake", "new", "-t", "templates#trivial", "flake2"]).run().ok().stderr_s
        assert f"wrote: {nix.env.dirs.home}/flake2/flake.nix" in stderr

    class TestNew:
        def test_idempotent(self, nix: Nix, files: Path):
            nix.nix(["flake", "new", "-t", "templates#trivial", files / "flake2"]).run().ok()
            stderr = (
                nix.nix(["flake", "new", "-t", "templates#trivial", files / "flake2"])
                .run()
                .ok()
                .stderr_s
            )
            assert "skipping identical file" in stderr

        def test_check(self, nix: Nix, files: Path):
            nix.nix(["flake", "new", "-t", "templates#trivial", files / "flake2"]).run().ok()
            stderr = nix.nix(["flake", "check", files / "flake2"]).run().ok().stderr_s
            assert "evaluating flake" in stderr
