from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.utils import get_global_asset_pack
from pathlib import Path


@with_files(
    {
        "flake1": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
                outputs = { self }: { x = import ./x.nix; };
            }"""),
            "x.nix": File("123"),
        },
        "flake2": get_global_asset_pack(".git")
        | {
            "flake.nix": File("""{
                outputs = { self, flake1 }: { x = flake1.x; };
            }""")
        },
    }
)
def test_unlocked_override(nix: Nix, files: Path, git: Git):
    nix.settings.add_xp_feature("nix-command", "flakes")

    git(files / "flake1", "add", "flake.nix", "x.nix")
    git(files / "flake1", "commit", "-m", "Initial")

    git(files / "flake2", "add", "flake.nix")

    assert (
        nix.nix(
            ["eval", "--json", f"{files}/flake2#x", "--override-input", "flake1", files / "flake1"]
        )
        .run()
        .ok()
        .stdout_s
        == "123\n"
    )

    (files / "flake1/x.nix").write_text("456")

    assert (
        nix.nix(
            ["eval", "--json", f"{files}/flake2#x", "--override-input", "flake1", files / "flake1"]
        )
        .run()
        .ok()
        .stdout_s
        == "456\n"
    )
