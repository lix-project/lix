from testlib.fixtures.file_helper import with_files, File
from testlib.fixtures.nix import Nix


@with_files({"foo": {"bar": File("bla")}})
def test_path_read_file(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")
    res = (
        nix.nix(
            [
                "eval",
                "--raw",
                "--impure",
                "--expr",
                f'builtins.readFile (builtins.toString (builtins.fetchTree {{ type = "path"; path = "{nix.env.dirs.home / "foo"}"; }} + "/bar"))',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == "bla"


@with_files({"foo": {"bar": File("bla")}})
def test_path_read_dir(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")
    res = (
        nix.nix(
            [
                "eval",
                "--json",
                "--impure",
                "--expr",
                f'builtins.readDir (builtins.toString (builtins.fetchTree {{ type = "path"; path = "{nix.env.dirs.home / "foo"}"; }}))',
            ]
        )
        .run()
        .ok()
    )
    assert res.stdout_plain == '{"bar":"regular"}'
