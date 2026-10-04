from pathlib import Path
import sqlite3

import aiohttp.web as web
import pytest
import sys

from testlib.fixtures.file_helper import File, with_files, CopyFile
from testlib.fixtures.http_server import http_server
from testlib.fixtures.nix import Nix, with_diverted_store
from testlib.utils import get_global_asset

pytestmark = pytest.mark.no_daemon


class HTTPStore:
    def __init__(self):
        self.uploaded_narinfos = {}
        self.uploaded_nars = {}
        self.known_nar_hashes = set()

    async def upload_narinfo(self, req: web.Request) -> web.Response:
        self.uploaded_narinfos[req.match_info["hash"]] = self._parse_narinfo(await req.text())
        return web.Response(text="")

    async def upload_nar(self, req: web.Request) -> web.Response:
        narhash = req.match_info["narhash"]
        self.known_nar_hashes.add(narhash)
        key = f"nar/{narhash}.nar"
        # This is intended for integration-tests with small data
        # as this buffers everything in memory.
        self.uploaded_nars[key] = await req.read()
        return web.Response(text="")

    async def serve_nar(self, req: web.Request) -> web.Response:
        narhash = req.match_info["narhash"]
        key = f"nar/{narhash}.nar"
        if body := self.uploaded_nars.get(key):
            return web.Response(body=body)
        return web.Response(text="", status=404)

    async def nix_cache_info(self, _: web.Request) -> web.Response:
        return web.Response(text="StoreDir: /nix/store")

    async def get_narinfo(self, req: web.Request) -> web.Response:
        narinfo_hash = req.match_info["hash"]
        if narinfo_hash not in self.uploaded_narinfos:
            return web.Response(text="", status=404)
        return web.Response(
            text="\n".join(f"{k}: {v}" for k, v in self.uploaded_narinfos[narinfo_hash].items())
            + "\n"
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

        self.uploaded_narinfos[req.match_info["hash"]] = narinfo
        return web.Response(text="")


class StoreWithIncompleteClosureAfterGC(HTTPStore):
    def __init__(self):
        super().__init__()
        self.make_hole_in_closure = False

    def simulate_gc(self):
        self.make_hole_in_closure = True

    async def get_narinfo(self, req: web.Request) -> web.Response:
        resp = await super().get_narinfo(req)
        try:
            store_path = self.uploaded_narinfos[req.match_info["hash"]]["URL"]
            if (
                store_path.endswith(("foo-b", "dependency-will-fail-to-substitute"))
                and self.make_hole_in_closure
            ):
                return web.Response(text="", status=404)
        except KeyError:
            return resp
        else:
            return resp

    async def serve_nar(self, req: web.Request) -> web.Response:
        uri = f"nar/{req.match_info['narhash']}.nar"
        try:
            nar = self.uploaded_nars[uri]
        except KeyError:
            return web.Response(text="", status=404)

        store_path = str(
            next((v["StorePath"] for v in self.uploaded_narinfos.values() if v["URL"] == uri), None)
        )

        if (
            store_path.endswith(("foo-b", "dependency-will-fail-to-substitute"))
            and self.make_hole_in_closure
        ):
            return web.Response(text="", status=404)
        return web.Response(body=nar)


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
            web.get("/nar/{narhash}.nar", store.serve_nar),
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


@with_files(
    {
        "config.nix": get_global_asset("config.nix"),
        "test-substituter-incomplete-closure.nix": CopyFile(
            "assets/test-substituter-incomplete-closure.nix"
        ),
    }
)
@pytest.mark.skipif(
    sys.platform == "darwin", reason="building in diverted store doesn't work on Darwin."
)
@with_diverted_store
def test_substituter_incomplete_closure(nix: Nix):
    """
    Regression-test for the first bug of https://git.lix.systems/lix-project/lix/issues/1291

    The case we're essentially having is

    * Multi-out derivation `foo` where output `a` depends on a leaf dependency
    * Derivation `bar` which depends on output `foo.b`.
    * Substituter was garbage-collected, i.e. the closure of `foo.a` fails to substitute
      and `foo.b` also fails to substitute, i.e. it is turned into a wanted output of
      derivation-goal `foo`.
    * While the narinfo endpoint serves a 404, narinfos are still in-cache which causes
      this failure specifically. On a real workload this bug could be prevented by decreasing
      the narinfo ttl to a low value.

    We get a SIGABRT with a failed assertion out of this when
    * `foo.a` is scheduled for a retry due to an incomplete closure error
      from its runtime closure.
    * `bar` discovers its dependency on `foo.b` in the meantime and causes
      `foo.b` to be added as wanted output to the derivation goal of `foo`.
      This has to happen before the retry of `foo.a` is taking place.
    """

    store = StoreWithIncompleteClosureAfterGC()
    outs = (
        nix.nix_build(["test-substituter-incomplete-closure.nix"])
        .run()
        .ok()
        .stdout_plain.splitlines()
    )
    app = start_server(store)
    with http_server(app) as httpd:
        url = f"http://localhost:{httpd.port}?compression=none&store=/nix/store&trusted=1"
        nix.nix(cmd=["store", "ping", "--store", url], flake=True).run().ok()

        nix.nix(cmd=["copy", *outs, "--to", url], flake=True).run().ok()
        assert len(store.known_nar_hashes) == 4

        nix.clear_store()
        store.simulate_gc()

        nix.nix_build(
            [
                "test-substituter-incomplete-closure.nix",
                "--option",
                "substituters",
                f"{url}&trusted=1",
                "--option",
                "max-substitution-jobs",
                "2",
                "--keep-going",
                "--option",
                "fallback",
                "true",
                "-vvv",
            ]
        ).run().ok()


@with_files({"test-file": File("hello world")})
@with_diverted_store
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
        assert hash_part in store.uploaded_narinfos

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
        assert nar_entries[0]["url"] == store.uploaded_narinfos[hash_part]["URL"]
