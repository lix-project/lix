from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


@with_files({"config.nix": get_global_asset("config.nix")})
def test_verify_store(nix: Nix):
    expr = """
        with import ./config.nix; mkDerivation {
            name = "foo";
            builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo";
        }
    """
    nix.nix_build(["-E", expr, "--no-out-link"]).run().ok()
    nix.nix_store(["--verify", "--check-contents"]).run().ok()
