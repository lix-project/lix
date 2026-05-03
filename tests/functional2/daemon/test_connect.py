import pytest
import itertools

from testlib.fixtures.nix import Nix


@pytest.fixture(autouse=True)
def setup(nix: Nix):
    nix.settings.add_xp_feature("nix-command")


def _observe_socket_order(nix: Nix, uri: str) -> list[str]:
    cmd = nix.nix(["store", "ping", "--store", uri, "--debug"]).run()
    cmd.expect(1)
    return [
        line.removeprefix("skipping socket ").split(":")[0]
        for line in cmd.stderr_s.splitlines()
        if line.startswith("skipping socket ")
    ]


def test_connection_order_plain(nix: Nix):
    assert _observe_socket_order(nix, f"unix://{nix.env.dirs.home}/socket") == [
        str(nix.env.dirs.home / "socket")
    ]


def test_connection_order_any(nix: Nix):
    assert _observe_socket_order(nix, f"unix://{nix.env.dirs.home}?protocol=any") == [
        str(nix.env.dirs.home / "lix-xp-1/socket"),
        str(nix.env.dirs.home / "socket"),
    ]


def test_connection_protocol_invalid(nix: Nix):
    cmd = nix.nix(["store", "ping", "--store", "unix:///dev/null?protocol=invalid"]).run()
    cmd.expect(1)
    assert "unsupported daemon protocol invalid" in cmd.stderr_s


def test_connection_protocol_any_not_standalone(nix: Nix):
    cmd = nix.nix(["store", "ping", "--store", "unix:///dev/null?protocol=any,any"]).run()
    cmd.expect(1)
    assert "unsupported daemon protocol any" in cmd.stderr_s


_protocols: dict[str, str] = {
    "lix-xp-1": "lix-xp-1/socket",
    "legacy-combined": ".",
    "legacy": "socket",
}


@pytest.mark.parametrize(
    ("protos", "sockets"),
    [
        (list(ps), [_protocols[p] for p in ps])
        for r in range(1, len(_protocols) + 1)
        for ps in itertools.permutations(_protocols, r)
    ],
)
def test_connection_order_specific_unix(nix: Nix, protos: list[str], sockets: list[str]):
    assert _observe_socket_order(
        nix, f"unix://{nix.env.dirs.home}?protocol={','.join(protos)}"
    ) == [str(nix.env.dirs.home / s) for s in sockets]


# this once can't handle legacy-combined sockets because those don't search paths
@pytest.mark.parametrize(
    ("protos", "sockets"),
    [
        (list(ps), [_protocols[p] for p in ps])
        for r in range(1, len(_protocols) + 1)
        for ps in itertools.permutations(
            {t: p for t, p in _protocols.items() if t != "legacy-combined"}, r
        )
    ],
)
def test_connection_order_specific_daemon_modern(nix: Nix, protos: list[str], sockets: list[str]):
    assert _observe_socket_order(nix, f"daemon?protocol={','.join(protos)}") == [
        str(nix.env.dirs.nix_state_dir / "daemon-socket" / s) for s in sockets
    ]


# this once can't handle legacy-combined sockets because those don't search paths
@pytest.mark.parametrize(
    ("protos", "sockets"),
    [
        (list(ps), [_protocols[p] for p in ps])
        for r in range(1, len(_protocols) + 1)
        for ps in itertools.permutations(
            {t: p for t, p in _protocols.items() if t != "legacy-combined"}, r
        )
    ],
)
def test_connection_order_specific_daemon_modern_relocated(
    nix: Nix, protos: list[str], sockets: list[str]
):
    nix.env["LIX_DAEMON_SOCKET_DIR"] = str(nix.env.dirs.home)
    assert _observe_socket_order(nix, f"daemon?protocol={','.join(protos)}") == [
        str(nix.env.dirs.home / s) for s in sockets
    ]
