from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import CopyFile, File, with_files
from testlib.utils import get_global_asset_pack
from testlib.environ import environ

import pytest

system = environ.get("system")


@with_files(
    get_global_asset_pack("simple-drv")
    | {
        "fmt.simple.sh": CopyFile("assets/fmt.simple.sh"),
        "flake.nix": File(f"""{{
          outputs = _: {{
            formatter.{system} =
              with import ./config.nix;
              mkDerivation {{
                name = "formatter";
                buildCommand = ''
                  mkdir -p $out/bin
                  echo "#! ${{shell}}" > $out/bin/formatter
                  cat ${{./fmt.simple.sh}} >> $out/bin/formatter
                  chmod +x $out/bin/formatter
                '';
              }};
          }};
        }}"""),
    }
)
class TestFmt:
    @pytest.fixture(autouse=True)
    def init(self, nix: Nix):
        nix.settings.add_xp_feature("nix-command", "flakes")

    def test_help(self, nix: Nix):
        assert "Format" in nix.nix(["fmt", "--help"]).run().ok().stdout_plain

    def test_no_args(self, nix: Nix):
        assert nix.nix(["fmt"]).run().ok().stdout_plain == "Formatting(0):"

    def test_forwarding(self, nix: Nix):
        assert (
            nix.nix(["fmt", "./file", "./folder"]).run().ok().stdout_plain
            == "Formatting(2): ./file ./folder"
        )

    def test_flake_check(self, nix: Nix):
        nix.nix(["flake", "check"]).run().ok()

    def test_flake_show(self, nix: Nix):
        assert "package 'formatter'" in nix.nix(["flake", "show"]).run().ok().stdout_plain
