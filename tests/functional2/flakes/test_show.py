from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, File
from testlib.environ import environ
from pathlib import Path
from .common import simple_flake
import pytest

system = environ.get("system")


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


@with_files(simple_flake())
class TestFlakeShow:
    def test_default_behavior(self, nix: Nix, files: Path):
        """
        By default: Only show the packages content for the current system and no legacyPackages at all
        """
        result = nix.nix(["flake", "show", "--json", files]).run().ok().json()
        assert result["packages"]["someOtherSystem"]["default"] == {}
        assert result["packages"][system]["default"]["name"] == "simple"
        assert result["legacyPackages"][system] == {}

    def test_eval_system_is_honored(self, nix: Nix, files: Path):
        result = (
            nix.nix(["flake", "show", "--eval-system", "someOtherSystem", "--json", files])
            .run()
            .ok()
            .json()
        )
        assert result["packages"]["someOtherSystem"]["default"]["name"] == "simple"

    def test_all_systems_shows_all(self, nix: Nix, files: Path):
        result = nix.nix(["flake", "show", "--json", "--all-systems", files]).run().ok().json()
        assert result["packages"]["someOtherSystem"]["default"]["name"] == "simple"
        assert result["legacyPackages"][system] == {}

    def test_legacy_flag_shows_legacy_packages(self, nix: Nix, files: Path):
        result = nix.nix(["flake", "show", "--json", "--legacy", files]).run().ok().json()
        assert result["legacyPackages"][system]["hello"]["name"] == "simple"


@with_files(
    {
        "flake.nix": File(f"""{{
          description = "Bla bla";

          outputs = inputs: rec {{
            apps.{system} = {{ }};
            checks.{system} = {{ }};
            devShells.{system} = {{ }};
            legacyPackages.{system} = {{ }};
            packages.{system} = {{ }};
            packages.someOtherSystem = {{ }};

            formatter = {{ }};
            nixosConfigurations = {{ }};
            nixosModules = {{ }};
          }};
        }}""")
    }
)
def test_show_without_contents(nix: Nix, files: Path):
    assert nix.nix(["flake", "show", "--json", "--all-systems", files]).run().ok().json() == {}


@with_files(
    simple_flake()
    | {
        "flake.nix": File(f"""{{
          outputs = inputs: {{
            legacyPackages.{system} = {{
              # nixpkgs.legacyPackages is a particularly prominent source of eval errors
              AAAAAASomeThingsFailToEvaluate = throw "nooo";
              simple = import ./simple.nix;
            }};
          }};
        }}""")
    }
)
def test_show_handles_eval_errors(nix: Nix, files: Path):
    result = (
        nix.nix(["flake", "show", "--json", "--legacy", "--all-systems", files]).run().ok().json()
    )
    assert result["legacyPackages"][system]["AAAAAASomeThingsFailToEvaluate"] == {}
    assert result["legacyPackages"][system]["simple"]["name"] == "simple"
