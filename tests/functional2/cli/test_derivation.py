import json
from testlib.utils import get_global_asset_pack
from testlib.fixtures.file_helper import with_files
from testlib.fixtures.nix import Nix


@with_files(get_global_asset_pack("simple-drv"))
def test_add_json(nix: Nix):
    drv_path = nix.nix_instantiate(["simple.nix"]).run().ok().stdout_plain

    drv_json = nix.nix(["derivation", "show", drv_path], flake=True).run().json()
    drv_json = next(iter(drv_json.values()))

    drv_path2 = (
        nix.nix(["derivation", "add"], flake=True)
        .with_stdin(json.dumps(drv_json).encode())
        .run()
        .ok()
        .stdout_plain
    )

    assert drv_path == drv_path2
    drv_json["name"] = "foo"

    res = (
        nix.nix(["derivation", "add"], flake=True)
        .with_stdin(json.dumps(drv_json).encode())
        .run()
        .expect(1)
    )

    assert "has incorrect output" in res.stderr_plain
