from pathlib import Path
from testlib.fixtures.file_helper import with_files
from testlib.utils import get_global_asset
from testlib.fixtures.nix import Nix


@with_files({"config.nix": get_global_asset("config.nix")})
def test_placeholders(nix: Nix):
    expr = """
      with import ./config.nix;

      mkDerivation {
        name = "placeholders";
        outputs = [ "out" "bin" "dev" ];
        buildCommand = "
          echo foo1 > $out
          echo foo2 > $bin
          echo foo3 > $dev
          [[ $(cat ${placeholder "out"}) = foo1 ]]
          [[ $(cat ${placeholder "bin"}) = foo2 ]]
          [[ $(cat ${placeholder "dev"}) = foo3 ]]
        ";
      }
    """

    result = Path(nix.nix_build(["-E", expr]).run().ok().stdout_plain)
    assert result.read_text() == "foo1\n"
