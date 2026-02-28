from pathlib import Path
from testlib.fixtures.nix import Nix
from testlib.fixtures.file_helper import File, with_files


@with_files({"hello": File("hello")})
def test_ssh_relay(nix: Nix):
    """
    test that nesting ssh stores via remote-store url parameters works.
    we don't currently check that each store spawns a new ssh instance,
    but if it didn't we'd almost certainly get remote execution errors.
    """

    nix.settings.add_xp_feature("nix-command")

    ssh_localhost = "ssh://localhost"
    store = ssh_localhost + 3 * f"?remote-store={ssh_localhost}"

    out = nix.nix(["store", "add-path", "--store", store, "hello"]).run().ok().stdout_plain
    assert Path(out).read_text() == "hello"
