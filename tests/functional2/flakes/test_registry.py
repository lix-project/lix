from testlib.fixtures.nix import Nix
import aiohttp.web as web
from testlib.fixtures.http_server import http_server
import pytest
import json


@pytest.mark.parametrize("registry", [None, "vendored"])
def test_default_registry(nix: Nix, registry: str | None):
    nix.settings.flake_registry = registry
    items = nix.nix(["registry", "list"], flake=True).run().ok().stdout_s.splitlines()
    # Make sure the vendored registry contains the correct amount.
    assert len(items) == 37
    # sanity check, contains the important ones
    assert any(item.startswith("global flake:nixpkgs") for item in items)
    assert any(item.startswith("global flake:home-manager") for item in items)


online_registry = {
    "flakes": [
        {
            "from": {"type": "indirect", "id": "nixpkgs"},
            "to": {"type": "github", "owner": "NixOS", "repo": "nixpkgs"},
        },
        {
            "from": {"type": "indirect", "id": "private-flake"},
            "to": {"type": "github", "owner": "fancy-enterprise", "repo": "private-flake"},
        },
    ],
    "version": 2,
}


class TestOnlineRegistry:
    async def get_registry(self, _req: web.Request) -> web.Response:
        return web.Response(text=json.dumps(online_registry))

    def test_online_registry(self, nix: Nix):
        app = web.Application()
        app.add_routes([web.get("/flake-registry.json", self.get_registry)])

        with http_server(app) as httpd:
            nix.settings.flake_registry = f"http://localhost:{httpd.port}/flake-registry.json"

            result = nix.nix(["registry", "list"], flake=True).run().ok()
            assert result.stdout_s.splitlines() == [
                "global flake:nixpkgs github:NixOS/nixpkgs",
                "global flake:private-flake github:fancy-enterprise/private-flake",
            ]

            # make sure there's a warning
            assert (
                "config option flake-registry referring to a URL is deprecated and will be removed"
                in result.stderr_s
            )
