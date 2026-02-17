from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.utils import get_global_asset_pack
from testlib.environ import environ
from pathlib import Path
from .common import simple_flake
import pytest

system = environ.get("system")
files = simple_flake() | {
    "b-low": simple_flake()
    | {
        "flake.nix": File(f"""{{
          outputs = inputs: rec {{
            packages.{system} = rec {{
              default =
                assert builtins.readFile ./message == "all good\n";
                assert builtins.readFile (inputs.self + "/message") == "all good\n";
                import ./simple.nix;
            }};
          }};
        }}"""),
        "message": File("all good\n"),
    }
}


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


@with_files(files)
def test_subdir_self_path(nix: Nix, files: Path):
    assert "simple> PATH=" in nix.nix(["build", f"{files}/b-low", "--no-link"]).run().ok().stderr_s


@with_files({"repo": files | get_global_asset_pack(".git"), "client": {}})
def test_git_subdir_self_path(nix: Nix, files: Path, git: Git):
    repo_dir = files / "repo"
    git(repo_dir, "add", ".")
    git(repo_dir, "commit", "-m", "init")

    client_dir = files / "client"
    (client_dir / "flake.nix").write_text(f"""{{
      inputs.inp = {{
        type = "git";
        url = "file://{repo_dir}";
        dir = "b-low";
      }};

      outputs = inputs: rec {{
        packages = inputs.inp.packages;
      }};
    }}""")

    assert "simple> PATH=" in nix.nix(["build", client_dir, "--no-link"]).run().ok().stderr_s
