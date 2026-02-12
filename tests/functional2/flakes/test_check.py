from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import FileDeclaration, with_files
from testlib.utils import File
from pathlib import Path
import pytest


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


def make_flake(text: str) -> FileDeclaration:
    return {"flake.nix": File(text)}


@pytest.mark.parametrize(
    "files",
    [
        make_flake("""{
          outputs = { self }: {
            overlay = final: prev: {};
          };
        }"""),
        make_flake("""{
          outputs = { self }: {
            overlay = finall: prev: {};
          };
        }"""),
    ],
    indirect=True,
)
def test_check_overlay_args_good(nix: Nix, files: Path):
    nix.nix(["flake", "check", str(files)]).run().ok()


@with_files(
    make_flake("""{
      outputs = { self }: {
        overlay = one: two: three: {};
      };
    }""")
)
def test_check_overlay_too_many_args(nix: Nix, files: Path):
    assert (
        "error: overlay is not a function with two arguments, but takes more than two"
        in nix.nix(["flake", "check", str(files)]).run().expect(1).stderr_s
    )


@with_files(
    make_flake("""{
      outputs = { self }: {
        overlay = one: {};
      };
    }""")
)
def test_check_overlay_not_enough_args(nix: Nix, files: Path):
    assert (
        "error: overlay is not a function with two arguments, but only takes one"
        in nix.nix(["flake", "check", str(files)]).run().expect(1).stderr_s
    )


@with_files(
    make_flake("""{
      outputs = { self, ... }: {
        overlays.x86_64-linux.foo = final: prev: {};
      };
    }""")
)
def test_check_overlay_not_a_function(nix: Nix, files: Path):
    assert (
        "error: overlay is not a function, but a set instead"
        in nix.nix(["flake", "check", str(files)]).run().expect(1).stderr_s
    )


@pytest.mark.parametrize(
    "files",
    [
        make_flake("""{
          outputs = { self }: {
            nixosModules.foo = {
              a.b.c = 123;
              foo = true;
            };
          };
        }"""),
        make_flake("""{
          outputs = { self }: {
            nixosModule = { config, pkgs, ... }: {
              a.b.c = 123;
            };
          };
        }"""),
    ],
    indirect=True,
)
def test_check_nixos_modules_good(nix: Nix, files: Path):
    nix.nix(["flake", "check", str(files)]).run().ok()


@with_files(
    make_flake("""{
      outputs = { self }: {
        nixosModules.foo = assert false; {
          a.b.c = 123;
          foo = true;
        };
      };
    }""")
)
def test_check_nixos_modules_eval_fail(nix: Nix, files: Path):
    assert "assertion failed" in nix.nix(["flake", "check", str(files)]).run().expect(1).stderr_s


@with_files(
    make_flake("""{
      outputs = { self }: {
        packages.system-1.default = "foo";
        packages.system-2.default = "bar";
      };
    }""")
)
class TestMultipleSystems:
    def test_check(self, nix: Nix, files: Path):
        nix.nix(["flake", "check", str(files)]).run().ok()

    def test_check_with_eval_system(self, nix: Nix, files: Path):
        """
        --eval-system should be considered for which the local system is for flake
        purposes, and thus it should fail checking that attr
        """
        assert (
            "'packages.system-1.default' is not a derivation"
            in nix.nix(["flake", "check", "--eval-system", "system-1", str(files)])
            .run()
            .expect(1)
            .stderr_s
        )

    def test_check_with_system(self, nix: Nix, files: Path):
        """
        --system should be considered for which the local system is for flake
        purposes, and thus it should fail checking that attr
        """
        assert (
            "'packages.system-1.default' is not a derivation"
            in nix.nix(["flake", "check", "--system", "system-1", str(files)])
            .run()
            .expect(1)
            .stderr_s
        )

    def test_check_all_systems(self, nix: Nix, files: Path):
        result = (
            nix.nix(["flake", "check", "--all-systems", "--keep-going", str(files)])
            .run()
            .expect(1)
            .stderr_s
        )
        assert "'packages.system-1.default' is not a derivation" in result
        assert "'packages.system-2.default' is not a derivation" in result
