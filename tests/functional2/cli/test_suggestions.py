import pytest
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import File, with_files


@with_files(
    {
        "flake.nix": File("""{
            outputs = a: {
               packages.system = {
                 foo = 1;
                 fo1 = 1;
                 fo2 = 1;
                 fooo = 1;
                 foooo = 1;
                 fooooo = 1;
                 fooooo1 = 1;
                 fooooo2 = 1;
                 fooooo3 = 1;
                 fooooo4 = 1;
                 fooooo5 = 1;
                 fooooo6 = 1;
               };
            };
        }""")
    }
)
class TestSuggestions:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix):
        nix.settings.add_xp_feature("nix-command", "flakes")
        nix.settings.system = "system"

    @pytest.mark.parametrize(
        "args",
        [
            [".#fob"],
            ["--impure", "--expr", "(builtins.getFlake (builtins.toPath ./.)).packages.system.fob"],
        ],
    )
    def test_three_closest(self, nix: Nix, args: list[str]):
        err = nix.nix(["build", *args]).run().expect(1).stderr_s
        assert "Did you mean one of fo1, fo2, foo or fooo?" in err

    def test_only_suggest_relevant(self, nix: Nix):
        assert "Did you mean" not in nix.nix(["build", ".#bar"]).run().expect(1).stderr_s

    def test_inactive_if_all_args_provided(self, nix: Nix):
        assert "Did you mean" not in (
            nix.nix(["build", "--impure", "--expr", "({ foo }: foo) { foo = 1; fob = 2; }"])
            .run()
            .expect(1)
            .stderr_s
        )

    def test_expr(self, nix: Nix):
        assert "Did you mean foo?" in (
            nix.nix(["build", "--impure", "--expr", "({ foo ? 1 }: foo) { fob = 2; }"])
            .run()
            .expect(1)
            .stderr_s
        )
