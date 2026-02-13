from testlib.fixtures.nix import Nix
from testlib.fixtures.git import Git
from testlib.fixtures.file_helper import with_files, File
from testlib.utils import get_global_asset_pack
from pathlib import Path


@with_files({"flake1": get_global_asset_pack(".git"), "input": File("input")})
def test_absolute_path(nix: Nix, files: Path, git: Git):
    nix.settings.add_xp_feature("nix-command", "flakes")

    flake = files / "flake1"
    (flake / "flake.nix").write_text(f"""{{
        outputs = {{ self }}: {{ x = builtins.readFile {files}/input; }};
    }}""")

    git(flake, "add", "flake.nix")
    git(flake, "commit", "-m", "Initial")

    assert nix.nix(["eval", "--impure", "--json", f"{flake}#x"]).run().ok().stdout_s == '"input"\n'
