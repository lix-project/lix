from pathlib import Path
from testlib.fixtures.file_helper import CopyFile
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset
from testlib.fixtures.nix import Nix


evil_drv = """
  builtins.derivation {
    name = "foo";
    builder = "/bin/sh";
    system = builtins.currentSystem;
    __structuredAttrs = true;

    __json.hello = "1";
    env.hello = "2";
  }
"""


def test_improper_structured_attrs_drv(nix: Nix):
    result = nix.nix_build(["--expr", evil_drv]).run().expect(1)
    assert "a `__json` attribute cannot be passed" in result.stderr_s


@with_files(
    {
        "structured-attrs.nix": CopyFile("assets/structured-attrs.nix"),
        "config.nix": get_global_asset("config.nix"),
    }
)
def test_proper_structured_attrs(nix: Nix, files: Path):
    result = files / "result"
    nix.nix_build(["structured-attrs.nix", "-A", "all", "-o", result]).run().ok()

    assert (result / "foo").read_text() == "bar\n"
    assert (files / "result-dev" / "foo").read_text() == "foo\n"
