from testlib.fixtures.file_helper import File, with_files
from testlib.fixtures.nix import Nix, NixDaemon

GUARD = "repairing is not allowed because you are not in 'trusted-users'"

# `--eval` alone will set the read only mode, hence `--read-write-mode`.
ADD_REPAIR = [
    "--eval",
    "--read-write-mode",
    "--repair",
    "-E",
    'builtins.path { name = "repair-test"; path = ./dummy; }',
]


@with_files({"dummy": File("hello")})
def test_untrusted_repair_in_add_to_store_is_rejected(nix: Nix, daemon: NixDaemon):
    with daemon(nix) as client:
        result = client.nix_instantiate(ADD_REPAIR).run()
        result.expect(1)
        assert GUARD in result.stderr_s


@with_files({"dummy": File("hello")})
def test_trusted_repair_in_add_to_store_is_allowed(nix: Nix, daemon: NixDaemon):
    with daemon(nix, [], {"trusted-users": "*"}) as client:
        client.nix_instantiate(ADD_REPAIR).run().ok()
