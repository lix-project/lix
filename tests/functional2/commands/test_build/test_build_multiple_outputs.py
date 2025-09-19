import pytest

from functional2.testlib.fixtures.file_helper import with_files, CopyFile
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset


_mo_files = {
    "multiple-outputs.nix": CopyFile("assets/test_build/multiple-outputs.nix"),
    "config.nix": get_global_asset("config.nix"),
}
_build_args = ["build", "-f", "multiple-outputs.nix", "--no-link", "--json"]


@with_files(_mo_files)
def test_build_outputs(nix: Nix):
    res = nix.nix([*_build_args, "a", "b"], flake=True).run().ok()
    a, b = res.json()
    assert a["drvPath"].endswith("multiple-outputs-a.drv")
    assert len(a["outputs"]) == 2
    assert a["outputs"]["first"].endswith("multiple-outputs-a-first")
    assert a["outputs"]["second"].endswith("multiple-outputs-a-second")

    assert b["drvPath"].endswith("multiple-outputs-b.drv")
    assert len(b["outputs"]) == 1
    assert b["outputs"]["out"].endswith("multiple-outputs-b")


@with_files(_mo_files)
def test_build_circumflex_0(nix: Nix):
    res = nix.nix([*_build_args, "a^first"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-a.drv")
    assert a["outputs"].keys() == {"first"}


@with_files(_mo_files)
def test_build_circumflex_1(nix: Nix):
    res = nix.nix([*_build_args, "a^second,first"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-a.drv")
    assert a["outputs"].keys() == {"first", "second"}


@with_files(_mo_files)
def test_build_circumflex_2(nix: Nix):
    res = nix.nix([*_build_args, "a^*"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-a.drv")
    assert a["outputs"].keys() == {"first", "second"}


@with_files(_mo_files)
def test_build_outputs_to_install(nix: Nix):
    res = nix.nix([*_build_args, "e"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-e.drv")
    assert a["outputs"].keys() == {"a_a", "b"}


@with_files(_mo_files)
def test_build_outputs_to_install_overridden_0(nix: Nix):
    res = nix.nix([*_build_args, "e^a_a"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-e.drv")
    assert a["outputs"].keys() == {"a_a"}


@with_files(_mo_files)
def test_build_outputs_to_install_overridden_1(nix: Nix):
    res = nix.nix([*_build_args, "e^*"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-e.drv")
    assert a["outputs"].keys() == {"a_a", "b", "c"}


@with_files(_mo_files)
def test_build_non_drv(nix: Nix):
    res = nix.nix([*_build_args, "e.a_a.outPath"], flake=True).run().ok()
    a = res.json()[0]
    assert a["drvPath"].endswith("multiple-outputs-e.drv")
    assert a["outputs"].keys() == {"a_a"}


@with_files(_mo_files)
def test_build_illegal_type(nix: Nix):
    res = nix.nix([*_build_args, "e.a_a.drvPath"], flake=True).run().expect(1)
    assert (
        "has a context which refers to a complete source and binary closure. This is not supported at this time"
        in res.stderr_s
    )


@with_files(_mo_files)
def test_build_no_context(nix: Nix):
    res = nix.nix(["build", "--expr", '""', "--no-link"], flake=True).run().expect(1)
    assert (
        "error: string '' has 0 entries in its context. It should only have exactly one entry"
        in res.stderr_s
    )


@with_files(_mo_files)
def test_build_too_much_context(nix: Nix):
    res = (
        nix.nix(
            [
                "build",
                "--impure",
                "--expr",
                'with (import ./multiple-outputs.nix).e.a_a; "${drvPath}${outPath}"',
            ],
            flake=True,
        )
        .run()
        .expect(1)
    )
    assert "has 2 entries in its context. It should only have exactly one entry" in res.stderr_s


@with_files(_mo_files)
def test_build_unsafe_discard(nix: Nix):
    res = (
        nix.nix(
            [
                "build",
                "--impure",
                "--json",
                "--expr",
                "builtins.unsafeDiscardOutputDependency (import ./multiple-outputs.nix).e.a_a.drvPath",
                "--no-link",
            ],
            flake=True,
        )
        .run()
        .ok()
    )
    assert res.json()[0].endswith("multiple-outputs-e.drv")


@pytest.fixture
def drv(nix: Nix) -> str:
    nix.settings.feature("nix-command")
    return (
        nix.nix(["eval", "-f", "multiple-outputs.nix", "--raw", "a.drvPath"], build=True)
        .run()
        .ok()
        .stdout_plain
    )


@with_files(_mo_files)
def test_build_not_an_output(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^not-an-output", "--no-link", "--json"]).run().expect(1)
    assert "does not have wanted outputs 'not-an-output'" in res.stderr_plain


@with_files(_mo_files)
def test_build_empty_output_list(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^", "--no-link", "--json"]).run().expect(1)
    assert "invalid extended outputs specifier" in res.stderr_plain


@with_files(_mo_files)
def test_build_star_entire_string(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^*nope", "--no-link", "--json"]).run().expect(1)
    assert "invalid extended outputs specifier" in res.stderr_plain


@with_files(_mo_files)
def test_build_drv_first(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^first", "--no-link", "--json"]).run().ok()
    out = res.json()[0]
    assert out["drvPath"].endswith("multiple-outputs-a.drv")
    assert out["outputs"].keys() == {"first"}
    assert out["outputs"]["first"].endswith("multiple-outputs-a-first")


@with_files(_mo_files)
def test_build_drv_first_second(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^first,second", "--no-link", "--json"]).run().ok()
    out = res.json()[0]
    assert out["drvPath"].endswith("multiple-outputs-a.drv")
    assert out["outputs"].keys() == {"first", "second"}
    assert out["outputs"]["first"].endswith("multiple-outputs-a-first")
    assert out["outputs"]["second"].endswith("multiple-outputs-a-second")


@with_files(_mo_files)
def test_build_drv_star(nix: Nix, drv: str):
    res = nix.nix(["build", f"{drv}^*", "--no-link", "--json"]).run().ok()
    out = res.json()[0]
    assert out["drvPath"].endswith("multiple-outputs-a.drv")
    assert out["outputs"].keys() == {"first", "second"}
    assert out["outputs"]["first"].endswith("multiple-outputs-a-first")
    assert out["outputs"]["second"].endswith("multiple-outputs-a-second")


@with_files(_mo_files)
def test_build_impure(nix: Nix):
    res = (
        nix.nix(
            ["build", "--impure", "-f", "multiple-outputs.nix", "--json", "e", "--no-link"],
            flake=True,
        )
        .run()
        .ok()
    )
    out = res.json()[0]
    assert out["drvPath"].endswith("multiple-outputs-e.drv")
    assert out["outputs"].keys() == {"a_a", "b"}


def test_build_stdin_empty(nix: Nix):
    res = (
        nix.nix(["build", "--no-link", "--stdin", "--json"], flake=True).with_stdin(b"").run().ok()
    )
    assert res.stdout_plain == "[]"


@with_files(_mo_files)
def test_build_stdin_stuff(nix: Nix, drv: str):
    cmd = nix.nix(["build", "--no-link", "--stdin", "--json"])
    res = cmd.with_stdin(f"{drv}^*\n".encode()).run().ok()
    out = res.json()[0]
    assert out["drvPath"].endswith("multiple-outputs-a.drv")
