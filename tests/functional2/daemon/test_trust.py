import pytest
import json

from testlib.fixtures.nix import Nix


@pytest.mark.parametrize(
    ("trusted", "flags", "expected"),
    [
        ([], ["--default-trust"], False),
        ([], ["--force-trusted"], True),
        (["*"], ["--default-trust"], True),
        (["*"], ["--force-untrusted"], False),
    ],
)
def test_trust(nix: Nix, trusted: list[str], flags: list[str], expected: bool):
    nix.settings.add_xp_feature("nix-command", "daemon-trust-override")

    with nix.daemon(flags, settings={"trusted-users": trusted}) as inner:
        trusted = json.loads(inner.nix(["store", "ping", "--json"]).run().ok().stdout)
        assert trusted["trusted"] == expected
