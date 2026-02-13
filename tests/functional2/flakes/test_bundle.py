from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, File
from testlib.environ import environ
from testlib.utils import get_global_asset_pack
import pytest

system = environ.get("system")

files = get_global_asset_pack("simple-drv") | {
    "flake.nix": File(f"""{{
        outputs = {{self}}: {{
          bundlers.{system} = rec {{
            simple = drv:
              if drv?type && drv.type == "derivation"
              then drv
              else self.packages.{system}.default;
            default = simple;
          }};
          packages.{system}.default = import ./simple.nix;
          apps.{system}.default = {{
            type = "app";
            program = "${{import ./simple.nix}}/hello";
          }};
        }};
    }}""")
}


@with_files(files)
@pytest.mark.parametrize(
    ("bundler", "attr"),
    [
        (".#", ".#"),
        (f".#bundlers.{system}.default", f".#packages.{system}.default"),
        (f".#bundlers.{system}.simple", f".#packages.{system}.default"),
        (f".#bundlers.{system}.default", f".#apps.{system}.default"),
        (f".#bundlers.{system}.simple", f".#apps.{system}.default"),
    ],
)
def test_bundle(nix: Nix, bundler: str, attr: str):
    nix.settings.add_xp_feature("nix-command", "flakes")
    nix.nix(["build", ".#"]).run().ok()
    nix.nix(["bundle", "--bundler", bundler, attr]).run().ok()
