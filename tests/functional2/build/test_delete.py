from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset

import json
from textwrap import dedent


@with_files(
    {
        "multiple-outputs.nix": get_global_asset("multiple-outputs.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_regression_6572_independent(nix: Nix):
    """
    https://github.com/NixOS/nix/issues/6572
    """
    nix.settings.add_xp_feature("nix-command")
    independent_text = (
        nix.nix(["build", "-f", "multiple-outputs.nix", "--json", "independent", "--no-link"])
        .run()
        .ok()
    )
    independent_json = json.loads(independent_text.stdout_plain)

    # Make sure that 'nix build' can build a derivation that depends on both outputs of another derivation.
    res = (
        nix.nix(
            [
                "build",
                "-f",
                "multiple-outputs.nix",
                "use-independent",
                "--no-link",
                "--print-out-paths",
            ]
        )
        .run()
        .ok()
    )
    path = res.stdout_plain

    res = nix.nix_store(["--delete", path]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    res = nix.nix_store(["--delete", independent_json[0]["outputs"]["first"]]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    res = (
        nix.nix(
            [
                "build",
                "-f",
                "multiple-outputs.nix",
                "use-independent",
                "--no-link",
                "--print-out-paths",
            ]
        )
        .run()
        .ok()
    )
    assert nix.physical_store_path_for(res.stdout_plain).read_text() == dedent("""\
        first
        second
    """)

    res = nix.nix_store(["--delete", path]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    # Make sure that 'nix build' tracks input-outputs correctly when a single output is already present.
    res = nix.nix_store(["--delete", independent_json[0]["outputs"]["second"]]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    res = (
        nix.nix(
            [
                "build",
                "-f",
                "multiple-outputs.nix",
                "use-independent",
                "--no-link",
                "--print-out-paths",
            ]
        )
        .run()
        .ok()
    )
    assert nix.physical_store_path_for(res.stdout_plain).read_text() == dedent("""\
        first
        second
    """)


@with_files(
    {
        "multiple-outputs.nix": get_global_asset("multiple-outputs.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_regression_6572_dependent(nix: Nix):
    """
    https://github.com/NixOS/nix/issues/6572
    """
    nix.settings.add_xp_feature("nix-command")
    res = nix.nix(["build", "-f", "multiple-outputs.nix", "--json", "a", "--no-link"]).run().ok()
    second_path = json.loads(res.stdout_plain)[0]["outputs"]["second"]

    path = (
        nix.nix(["build", "-f", "multiple-outputs.nix", "use-a", "--no-link", "--print-out-paths"])
        .run()
        .ok()
        .stdout_plain
    )
    res = nix.nix_store(["--delete", path]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    res = nix.nix_store(["--delete", second_path]).run().ok()
    assert "1 store paths deleted" in res.stderr_plain

    res = (
        nix.nix(["build", "-f", "multiple-outputs.nix", "use-a", "--no-link", "--print-out-paths"])
        .run()
        .ok()
    )
    assert nix.physical_store_path_for(res.stdout_plain).read_text() == dedent("""\
        first
        second
    """)
