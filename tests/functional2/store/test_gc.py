from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset


@with_files({"config.nix": get_global_asset("config.nix")})
def test_selfref_gc(nix: Nix):
    expr = """
        with import ./config.nix;

        let d1 = mkDerivation {
          name = "selfref-gc";
          outputs = [ "out" ];
          buildCommand = "
            echo SELF_REF: $out > $out
          ";
        }; in

        # the only change from d1 is d1 as an (unused) build input
        # to get identical store path in CA.
        mkDerivation {
          name = "selfref-gc";
          outputs = [ "out" ];
          buildCommand = "
            echo UNUSED: ${d1}
            echo SELF_REF: $out > $out
          ";
        }
    """

    out = Path(nix.nix_build(["--no-out-link", "-E", expr]).run().ok().stdout_plain)
    assert out.exists()

    nix.nix([], nix_exe="nix-collect-garbage").run().ok()

    assert list(nix.env.dirs.real_store_dir.glob("*selfref*")) == []
