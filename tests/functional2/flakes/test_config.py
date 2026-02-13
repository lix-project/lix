from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, File
from testlib.environ import environ
from testlib.utils import get_global_asset_pack
from pathlib import Path
from textwrap import dedent
import pytest

system = environ.get("system")

files = get_global_asset_pack("simple-drv") | {
    "flake.nix": File(f"""
        {{
            nixConfig.post-build-hook = ./echoing-post-hook.sh;
            nixConfig.allow-dirty = false; # See #5621

            outputs = a: {{
               packages.{system}.default = import ./simple.nix;
            }};
        }}
    """)
}


@pytest.fixture(autouse=True)
def common_init(nix: Nix, files: Path):
    nix.settings.add_xp_feature("nix-command", "flakes")

    hook = files / "echoing-post-hook.sh"
    hook.write_text(
        dedent(f"""\
        #!/bin/sh

        echo "ThePostHookRan as $0" > {files}/post-hook-ran
    """)
    )
    hook.chmod(0o755)


@with_files(files)
class TestFlakeConfig:
    def test_post_hook_ignored_without_accept_config(self, nix: Nix, files: Path):
        nix.nix(["build"]).run().ok()
        assert not (files / "post-hook-ran").exists()

    def test_post_hook_ignored_with_no_accept_config(self, nix: Nix, files: Path):
        nix.nix(["build", "--no-accept-flake-config"]).run().ok()
        assert not (files / "post-hook-ran").exists()

    def test_post_hook_runs_with_accept_config(self, nix: Nix, files: Path):
        nix.nix(["build", "--accept-flake-config"]).run().ok()

        hook_output = files / "post-hook-ran"
        assert hook_output.exists()
        first_output = hook_output.read_text()

        # Make sure that the path to the post hook doesn't change if we change
        # something in the flake.
        # Otherwise the user would have to re-validate the setting each time.
        flake = files / "flake.nix"
        flake.write_text(flake.read_text() + "# comment\n")
        nix.clear_store()
        nix.nix(["build", "--accept-flake-config"]).run().ok()
        assert hook_output.read_text() == first_output
