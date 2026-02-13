from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.utils import get_global_asset_pack
from pathlib import Path


@with_files(
    {
        "flakeA": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
          inputs.b.url = "git+file://$flakeB";
          inputs.b.inputs.a.follows = "/";

          outputs = { self, b }: {
            foo = 123 + b.bar;
            xyzzy = 1000;
          };
        }""")
        },
        "flakeB": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
          inputs.a.url = "git+file://$flakeA";

          outputs = { self, a }: {
            bar = 456 + a.xyzzy;
          };
        }""")
        },
    }
)
def test_circular(nix: Nix, files: Path, git: Git):
    nix.settings.add_xp_feature("nix-command", "flakes")

    path = files / "flakeA/flake.nix"
    path.write_text(path.read_text().replace("$flakeB", str(files / "flakeB")))
    git(files / "flakeA", "add", "flake.nix")

    path = files / "flakeB/flake.nix"
    path.write_text(path.read_text().replace("$flakeA", str(files / "flakeA")))
    git(files / "flakeB", "add", "flake.nix")
    git(files / "flakeB", "commit", "-a", "-m", "Foo")

    assert nix.nix(["eval", f"{files}/flakeA#foo"]).run().ok().stdout_s == "1579\n"

    path = files / "flakeB/flake.nix"
    path.write_text(path.read_text().replace("456", "789"))
    git(files / "flakeB", "commit", "-a", "-m", "Foo")
    nix.nix(["flake", "update", "b", "--flake", f"{files}/flakeA"]).run().ok()
    assert nix.nix(["eval", f"{files}/flakeA#foo"]).run().ok().stdout_s == "1912\n"

    # Test list-inputs with circular dependencies
    nix.nix(["flake", "metadata", f"{files}/flakeA"]).run().ok()
