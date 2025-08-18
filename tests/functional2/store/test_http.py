from pathlib import Path
import sqlite3

import aiohttp.web as web
import pytest

from functional2.testlib.fixtures.file_helper import File, with_files
from functional2.testlib.fixtures.http_server import http_server
from functional2.testlib.fixtures.nix import Nix


class HTTPStore:
    def __init__(self):
        self.uploaded_nars = {}
        self.known_nar_hashes = set()

    async def upload_narinfo(self, req: web.Request) -> web.Response:
        self.uploaded_nars[req.match_info["hash"]] = self._parse_narinfo(await req.text())
        return web.Response(text="")

    async def upload_nar(self, req: web.Request) -> web.Response:
        self.known_nar_hashes.add(req.match_info["narhash"])
        return web.Response(text="")

    async def nix_cache_info(self, _: web.Request) -> web.Response:
        return web.Response(text="StoreDir: /nix/store")

    async def get_narinfo(self, req: web.Request) -> web.Response:
        narinfo_hash = req.match_info["hash"]
        if narinfo_hash not in self.uploaded_nars:
            return web.Response(text="", status=404)
        return web.Response(
            text="\n".join(f"{k}: {v}" for k, v in self.uploaded_nars[narinfo_hash].items()) + "\n"
        )

    async def nar_head(self, req: web.Request) -> web.Response:
        return web.Response(
            text="", status=200 if req.match_info["narhash"] in self.known_nar_hashes else 404
        )

    def _parse_narinfo(self, text: str) -> dict[str, str]:
        narinfo = {}
        for line in text.splitlines():
            key, value = line.split(": ", 1)
            narinfo[key] = value
        _, hashpart = narinfo["FileHash"].split(":", 1)
        assert hashpart in self.known_nar_hashes
        return narinfo


class FakeNARBridge(HTTPStore):
    """
    HTTP Store that mutates the URL field of the narinfo, just like
    nar-bridge from snix.dev.

    Testcase to ensure that the correct URL to the nar (i.e. nar/snix-castore/...)
    ends up in the disk-cache.
    """

    async def upload_narinfo(self, req: web.Request) -> web.Response:
        narinfo = self._parse_narinfo(await req.text())
        narinfo["URL"] = (
            f"nar/snix-castore/00000000000000000000000000000000000000000000000000000?narsize=f{narinfo['FileSize']}"
        )

        self.uploaded_nars[req.match_info["hash"]] = narinfo
        return web.Response(text="")


@pytest.fixture(params=[HTTPStore, FakeNARBridge])
def store(request: pytest.FixtureRequest) -> HTTPStore:
    store_class = request.param
    return store_class()


def start_server(store: HTTPStore) -> web.Application:
    app = web.Application()
    app.add_routes(
        [
            web.put("/{hash}.narinfo", store.upload_narinfo),
            web.get("/{hash}.narinfo", store.get_narinfo),
            web.put("/nar/{narhash}.nar", store.upload_nar),
            web.get("/nix-cache-info", store.nix_cache_info),
            web.head("/nar/{narhash}.nar", store.nar_head),
        ]
    )

    return app


def nars_from_narinfo_cache(db_path: Path) -> list[dict[str, str | bool]]:
    assert db_path.exists()
    db = sqlite3.connect(db_path)
    rows = db.execute(
        "SELECT n.present, n.hashPart, n.namePart, n.url FROM NARs n INNER JOIN BinaryCaches b ON n.cache = b.id WHERE b.url LIKE '%localhost%'"
    )
    return [
        {"present": bool(present), "hashPart": hashPart, "namePart": namePart, "url": url}
        for present, hashPart, namePart, url in rows
    ]


@with_files({"test-file": File("hello world")})
def test_http_simple(nix: Nix, store: HTTPStore, files: Path):
    test_file = files / "test-file"
    result = nix.nix(cmd=["store", "add-file", test_file], flake=True).run()
    result.ok()
    store_path = result.stdout_plain
    hash_part, _ = Path(store_path).stem.split("-", 1)

    nar_info_cache = nix.env.dirs.xdg_cache_home / "nix" / "binary-cache-v6.sqlite"

    app = start_server(store)
    with http_server(app) as httpd:
        url = f"http://localhost:{httpd.port}?compression=none&store=/nix/store"
        nix.nix(cmd=["store", "ping", "--store", url], flake=True).run().ok()

        # Narinfo shouldn't exist yet
        nix.nix(cmd=["path-info", "--store", url, store_path], flake=True).run().expect(1)
        cache_entries = nars_from_narinfo_cache(nar_info_cache)

        assert len(cache_entries) == 1
        assert not cache_entries[0]["present"]
        assert cache_entries[0]["hashPart"] == hash_part

        # Successful upload
        nix.nix(
            cmd=["copy", "--from", nix.settings.store, "--to", url, store_path], flake=True
        ).run().ok()
        assert hash_part in store.uploaded_nars

        # Make sure the negative entry got removed
        cache_entries = nars_from_narinfo_cache(nar_info_cache)
        assert len(cache_entries) == 0

        # Ensure that the narinfo can be found now.
        nix.nix(cmd=["path-info", "--store", url, store_path], flake=True).run().ok()

        # Ensure local narinfo cache is up-to-date.
        nar_entries = nars_from_narinfo_cache(nar_info_cache)
        assert len(nar_entries) == 1

        assert nar_entries[0]["present"]
        assert nar_entries[0]["hashPart"] == hash_part
        assert nar_entries[0]["namePart"] == "test-file"
        assert nar_entries[0]["url"] == store.uploaded_nars[hash_part]["URL"]
