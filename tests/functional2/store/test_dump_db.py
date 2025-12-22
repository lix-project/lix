import shutil

from functional2.testlib.fixtures.file_helper import with_files
from functional2.testlib.fixtures.nix import Nix
from functional2.testlib.utils import get_global_asset_pack


@with_files(get_global_asset_pack("dependencies"))
def test_dump_db(nix: Nix):
    nix.nix_build(["dependencies.nix", "-o", "result"]).run().ok()

    res = nix.nix_store(["-qR", "result"], build=True).run().ok()
    deps = res.stdout_plain

    res = nix.nix_store(["--dump-db"], build=True).run().ok()
    dump = res.stdout

    shutil.rmtree(nix.env.dirs.nix_state_dir / "db")

    nix.nix_store(["--load-db"], build=True).with_stdin(dump).run().ok()

    res = nix.nix_store(["-qR", "result"], build=True).run().ok()
    deps2 = res.stdout_plain

    assert deps == deps2

    res = nix.nix_store(["--dump-db"], build=True).run().ok()
    dump2 = res.stdout

    assert dump == dump2
