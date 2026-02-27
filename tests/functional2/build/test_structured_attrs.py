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
