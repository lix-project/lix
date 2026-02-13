from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.environ import environ
from pathlib import Path
import pytest
from .common import simple_flake

system = environ.get("system")

files = simple_flake() | {"foo": {"flake.nix": File("{ outputs = _: {}; }")}, "subdir": {}}


@pytest.fixture(autouse=True)
def common_init(nix: Nix, files: Path):
    nix.settings.add_xp_feature("nix-command", "flakes")

    flake = files / "flake.nix"
    flake.write_text(f"""
        {{
            inputs.foo.url = "{files}/foo";
            outputs = a: {{
               packages.{system} = rec {{
                 test = import ./simple.nix;
                 default = test;
               }};
            }};
        }}
    """)


good_uris: list[list[str]] = [
    [],
    ["."],
    [".#"],
    [".#test"],
    ["../subdir"],
    ["../subdir#test"],
    ["$PWD"],
]


@with_files(files)
class TestFlakeSearchPath:
    @pytest.mark.parametrize("args", good_uris)
    def test_flake_search_goes_up(self, nix: Nix, files: Path, args: list[str]):
        subdir = files / "subdir"
        nix.nix(
            ["build", *[arg.replace("$PWD", str(subdir)) for arg in args]], cwd=subdir
        ).run().ok()

    def test_flake_search_inactive_with_path_uri(self, nix: Nix, files: Path):
        subdir = files / "subdir"
        assert (
            "does not contain a '/flake.nix' file"
            in nix.nix(["build", f"path:{subdir}"], cwd=subdir).run().expect(1).stderr_s
        )

    def test_flake_search_goes_up_without_installable(self, nix: Nix):
        nix.nix(["build", "--override-input", "foo", "."]).run().ok()

    def test_flake_inputs_do_not_search(self, nix: Nix, files: Path):
        flake = files / "flake.nix"
        flake.write_text(flake.read_text().replace(str(files / "foo"), str(files / "foo/subdir")))
        nix.nix(["build"]).run().expect(1)

    @pytest.mark.parametrize("args", good_uris)
    def test_flake_search_does_not_cross_git_repo(
        self, git: Git, nix: Nix, files: Path, args: list[str]
    ):
        subdir = files / "subdir"
        git(subdir, "init")
        assert (
            "is not part of a flake"
            in nix.nix(["build", *[arg.replace("$PWD", str(subdir)) for arg in args]], cwd=subdir)
            .run()
            .expect(1)
            .stderr_s
        )

    def test_flake_search_does_not_cross_git_repo_with_path_ru(
        self, git: Git, nix: Nix, files: Path
    ):
        subdir = files / "subdir"
        git(subdir, "init")
        assert (
            "does not contain a '/flake.nix' file"
            in nix.nix(["build", f"path:{subdir}"], cwd=subdir).run().expect(1).stderr_s
        )
