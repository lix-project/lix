from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files, File
from pathlib import Path
import pytest


@pytest.fixture(autouse=True)
def common_init(nix: Nix):
    nix.settings.add_xp_feature("nix-command", "flakes")


# Trivial let bindings should work within a flake
@with_files(
    {
        "flake.nix": File("""
            let
              description = "meow";
            in {
              inherit description;
              inputs = { };

              outputs = { self }: { };
            }
        """)
    }
)
def test_trivial_let(nix: Nix, files: Path):
    assert nix.nix(["flake", "show", "--json", files]).run().ok().json() == {}


# Interpolation is not a function call and thus allowed (it desugars to string concatenation)
@with_files(
    {
        "dependency": {"flake.nix": File("{ outputs = _: {}; }")},
        "flake.nix": File("""
            let
              src = "path:.";
            in {
              inputs = {
                lix.url = "${src}/dependency";
              };

              outputs = { self, lix }: {
              };
            }
        """),
    }
)
def test_trivial_interpolation(nix: Nix, files: Path):
    assert nix.nix(["flake", "show", "--json", files]).run().ok().json() == {}


# `-1` desugars to `__sub 0 1` and thus is –stupidly– forbidden.
# Once we have finalized the deprecation of shadowing of internal symbols, we will be able to change subtraction and division
# to use proper AST nodes. Like they should have in the first place.
@with_files(
    {
        "flake.nix": File("""
            {
              inputs = -1;

              outputs = { self, lix }: { };
            }
        """)
    }
)
def test_trivial_implicit_function(nix: Nix, files: Path):
    assert (
        "error: stack overflow"
        in nix.nix(["flake", "show", "--json", files]).run().expect(1).stderr_plain
    )


@with_files(
    {
        "flake.nix": File("""
            {
              inputs = builtins.seq true { };

              outputs = { self, lix }: { };
            }
        """)
    }
)
def test_trivial_explicit_function(nix: Nix, files: Path):
    assert (
        "error: stack overflow"
        in nix.nix(["flake", "show", "--json", files]).run().expect(1).stderr_plain
    )
