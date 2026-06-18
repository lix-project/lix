from pathlib import Path

from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


@with_files({"config.nix": get_global_asset("config.nix")})
def test_sign(nix: Nix, tmp_path: Path):
    nix.settings.add_xp_feature("nix-command")

    sk = tmp_path / "sk"
    pk = tmp_path / "pk"
    nix.nix_store(["--generate-binary-cache-key", "cache.example.org", sk, pk]).run().ok()

    expr = """
        with import ./config.nix; mkDerivation {
            name = "foo";
            builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo";
        }
    """
    # Build and check unsigned
    # For some reason the signing info is only exposed with `--json`, probably the CLI being badly designed once again?
    out = nix.nix_build(["-E", expr, "--no-out-link"]).run().ok().stdout_plain
    assert "cache.example.org" not in nix.nix(["path-info", "--json", out]).run().ok().stdout_plain

    # Sign and then check signed
    nix.nix(["store", "sign", "--key-file", sk, out]).run().ok()
    assert "cache.example.org" in nix.nix(["path-info", "--json", out]).run().ok().stdout_plain
