import pytest
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import File, with_files


class TestImpureEval:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix):
        nix.settings.add_xp_feature("nix-command")
        self.nix = nix

    def eval(self, expr: str, *args: str) -> str:
        return (
            self.nix.nix(["eval", "--impure", "--raw", *args, "--expr", expr])
            .run()
            .ok()
            .stdout_plain
        )

    @pytest.mark.parametrize("path", ["/foo", "/bar"])
    def test_store_dir(self, path: str):
        result = self.eval("builtins.storeDir", "--store", f"dummy://?store={path}")
        assert result == path

    @pytest.mark.parametrize("system", ["foo", "bar"])
    def test_current_system_system_only(self, system: str):
        result = self.eval("builtins.currentSystem", "--system", system)
        assert result == system

    @pytest.mark.parametrize("system", ["foo", "bar"])
    def test_current_system_both(self, system: str):
        result = self.eval("builtins.currentSystem", "--system", system, "--eval-system", system)
        assert result == system

    @pytest.mark.parametrize("system", ["foo", "bar"])
    def test_current_system_eval_system_only(self, system: str):
        result = self.eval("builtins.currentSystem", "--eval-system", system)
        assert result == system

    @pytest.mark.parametrize("system", ["foo", "bar"])
    def test_current_system_both_override(self, system: str):
        result = self.eval("builtins.currentSystem", "--system", system, "--eval-system", "baz")
        assert result == "baz"

    @with_files({"eval.nix": File('{ attr.foo = "bar"; }')})
    def test_nix_path_search(self, nix: Nix):
        result = (
            nix.nix(
                [
                    "eval",
                    "--option",
                    "nix-path",
                    f"foo={nix.env.dirs.home}",
                    "-f",
                    "<foo/eval.nix>",
                    "attr.foo",
                ]
            )
            .run()
            .ok()
        )
        assert result.stdout_plain == '"bar"'
