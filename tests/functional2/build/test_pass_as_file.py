from testlib.fixtures.nix import Nix
from testlib.utils import get_global_asset
from testlib.fixtures.file_helper import File, with_files


@with_files(
    {
        "config.nix": get_global_asset("config.nix"),
        "default.nix": File("""
            with import ./config.nix;

            mkDerivation {
              name = "pass-as-file";
              passAsFile = [ "foo" ];
              foo = [ "xyzzy" ];
              builder = builtins.toFile "builder.sh" ''
                [ "$(basename $fooPath)" = .attr-1bp7cri8hplaz6hbz0v4f0nl44rl84q1sg25kgwqzipzd1mv89ic ]
                [ "$(cat $fooPath)" = xyzzy ]
                touch $out
              '';
            }
        """),
    }
)
def test_pass_as_file(nix: Nix):
    nix.nix_build([]).run().ok()
