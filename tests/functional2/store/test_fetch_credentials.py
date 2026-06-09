import ssl
import subprocess
from pathlib import Path

import aiohttp.web as web
import pytest

from testlib.fixtures.file_helper import File, FileDeclaration, with_files
from testlib.fixtures.http_server import http_server
from testlib.fixtures.nix import Nix

ca_key: str = """
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEICjSh37n2iKiAwJZe2nPDpla9LCL2du3dbPWIto9XlqjoAoGCCqGSM49
AwEHoUQDQgAEWZ2yB2EiLBY6fioAX4z7KMcW2qBxlGBZQ92rkQR8FaENtgfJsQyJ
KXO/dnTi5oismS0p7IYTX4q7mtXw88Xdew==
-----END EC PRIVATE KEY-----
"""

server_key: str = """
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIMulMNf+67kZv7xFfKPhnQM1wXstjDB6q17vNL3k0fPwoAoGCCqGSM49
AwEHoUQDQgAEo6gUBzg5TsjvHszViHq4u8j/5dQa/Hu6ovWhgu/8xHS2+/G28ywG
5MhcjBT/neb0wAYRErCRi8b57Bb6Yxuc8w==
-----END EC PRIVATE KEY-----
"""

content: str = "mrrreow"
ssl_files: FileDeclaration = {"ca.key": File(ca_key), "server.key": File(server_key)}


@pytest.fixture(autouse=True)
def create_certs(files: Path):
    # Add the certificates to the file dict
    assert (
        subprocess.run(
            [
                "openssl",
                "req",
                "-new",
                "-x509",
                "-days",
                "1",
                "-key",
                files / "ca.key",
                "-out",
                files / "ca.crt",
                "-subj",
                "/O=LixTestCA",
                "-addext",
                "basicConstraints=critical,CA:TRUE",
                "-addext",
                "keyUsage=critical,keyCertSign,cRLSign",
            ]
        ).returncode
        == 0
    )
    assert (
        subprocess.run(
            [
                "openssl",
                "req",
                "-new",
                "-key",
                files / "server.key",
                "-out",
                files / "server.csr",
                "-subj",
                "/CN=localhost",
            ]
        ).returncode
        == 0
    )
    assert (
        subprocess.run(
            [
                "openssl",
                "x509",
                "-req",
                "-days",
                "1",
                "-in",
                files / "server.csr",
                "-CA",
                files / "ca.crt",
                "-CAkey",
                files / "ca.key",
                "-sha256",
                "-set_serial",
                "01",
                "-out",
                files / "server.crt",
            ]
        ).returncode
        == 0
    )


@pytest.fixture(autouse=True)
def setup(nix: Nix) -> None:
    nix.settings.add_xp_feature("nix-command")


async def uwu(_: web.Request) -> web.Response:  # noqa: RUF029
    return web.Response(text=content)


async def mrrp(req: web.Request) -> web.Response:  # noqa: RUF029
    if (
        "Authorization" in req.headers
        and req.headers["Authorization"] == "Basic YW5vbnltb3VzOm55YQ=="
    ):
        return web.Response(text=content)
    return web.Response(status=401)


@pytest.fixture
def app() -> web.Application:
    # Create an app that provides a file
    app = web.Application()
    app.add_routes([web.get("/uwu", uwu), web.get("/mrrp", mrrp)])
    return app


@pytest.fixture
def ssl_context(files: Path) -> ssl.SSLContext:
    # Create a ssl context that uses the ca signed certs
    context = ssl.create_default_context(purpose=ssl.Purpose.CLIENT_AUTH)
    context.load_cert_chain(files / "server.crt", files / "server.key")
    context.check_hostname = False
    return context


def get_params(
    port: int, cert: str = "", netrc: str = "", secure: bool = False, auth: bool = False
) -> list[str]:
    return (
        [
            "--offline",
            "store",
            "prefetch-file",
            f"{'https' if secure or cert else 'http'}://localhost:{port}/{'mrrp' if netrc or auth else 'uwu'}",
        ]
        + (["--option", "ssl-cert-file", cert] if cert else [])
        + (["--option", "netrc-file", netrc] if netrc else [])
    )


@with_files(ssl_files)
def test_certs(files: Path):
    # Test if certs actually exist
    assert (files / "ca.crt").exists()
    assert "-----BEGIN CERTIFICATE-----" in (files / "ca.crt").read_text()
    assert "-----END CERTIFICATE-----" in (files / "ca.crt").read_text()
    assert (files / "server.crt").exists()
    assert "-----BEGIN CERTIFICATE-----" in (files / "server.crt").read_text()
    assert "-----END CERTIFICATE-----" in (files / "server.crt").read_text()


@with_files(ssl_files)
def test_fetch_success(nix: Nix, app: web.Application) -> None:
    # Test if normal HTTP traffic works
    with http_server(app) as httpd:
        parameters = get_params(httpd.port)

        response = nix.nix(parameters).run().ok().stderr_plain

        assert f"Downloaded '{parameters[3]}' to '{nix.env.dirs.nix_store_dir}/" in response
        assert "-uwu' (hash 'sha256-4uSfns8lpq5mZItbtpkOGcQjk7hqLjHF8OhrJt+n4Cw=')." in response


@with_files(ssl_files)
def test_fetch_ssl(
    nix: Nix, files: Path, app: web.Application, ssl_context: ssl.SSLContext
) -> None:
    # Test that using the correct cert works
    with http_server(app, ssl_context=ssl_context) as httpd:
        parameters = get_params(httpd.port, cert=(str)(files / "ca.crt"))
        response = nix.nix(parameters).run().ok().stderr_plain

        assert f"Downloaded '{parameters[3]}' to '{nix.env.dirs.nix_store_dir}/" in response
        assert "-uwu' (hash 'sha256-4uSfns8lpq5mZItbtpkOGcQjk7hqLjHF8OhrJt+n4Cw=')." in response


@with_files(ssl_files)
def test_fetch_missing_ca_option(
    nix: Nix, app: web.Application, ssl_context: ssl.SSLContext
) -> None:
    # Test that using no cert leads to a curl error
    with http_server(app, ssl_context=ssl_context) as httpd:
        parameters = get_params(httpd.port, secure=True)

        error = nix.nix(parameters).run().expect(1).stderr_plain

        assert (
            error
            == f"error: unable to download '{parameters[3]}': SSL certificate OpenSSL verify result: unable to get local issuer certificate (20) (curl error code=60)"
        )


@with_files(ssl_files)
def test_fetch_missing_cafile(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that a missing cert file returns an error
    with http_server(app) as httpd:
        crt_file: str = (str)(files / "missing.crt")

        error = nix.nix(get_params(httpd.port, cert=crt_file)).run().expect(1).stderr_plain

        assert error == f"error: ca file does not exist at specified location '{crt_file}'"


@with_files(ssl_files)
def test_fetch_empty_cafile(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that an empty cert file leads to a curl error
    with http_server(app) as httpd:
        crt_file: Path = files / "empty.crt"
        crt_file.write_text("")
        parameters: list[str] = get_params(httpd.port, cert=(str)(crt_file))

        error = nix.nix(parameters).run().expect(1).stderr_plain

        assert (
            error
            == f"error: unable to download '{parameters[3]}': error adding trust anchors from file: {crt_file} (curl error code=77)"
        )


@with_files(ssl_files)
def test_fetch_missing_netrc_option(nix: Nix, app: web.Application) -> None:
    # Test that using no netrc file leads to unauthorized response
    with http_server(app) as httpd:
        parameters = get_params(httpd.port, auth=True)

        error = nix.nix(parameters).run().expect(1).stderr_plain

        assert (
            f"error: unable to download '{parameters[3]}': HTTP error 401 (Unauthorized)" in error
        )


@with_files(ssl_files)
def test_fetch_missing_netrc(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that a missing netrc file leads to an error
    with http_server(app) as httpd:
        netrc_file: str = (str)(files / "missing-netrc")

        error = nix.nix(get_params(httpd.port, netrc=netrc_file)).run().expect(1).stderr_plain

        assert error == f"error: netrc file does not exist at specified location '{netrc_file}'"


@with_files(ssl_files)
def test_fetch_empty_netrc(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that an empty netrc file leads to unauthorized response
    with http_server(app) as httpd:
        netrc_file: Path = files / "empty-netrc"
        netrc_file.write_text("")
        parameters: list[str] = get_params(httpd.port, netrc=(str)(netrc_file))

        error = nix.nix(parameters).run().expect(1).stderr_plain

        assert (
            f"error: unable to download '{parameters[3]}': HTTP error 401 (Unauthorized)" in error
        )


@with_files(ssl_files)
def test_fetch_netrc(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that the correct netrc file succeeds
    with http_server(app) as httpd:
        netrc_file: Path = files / "empty-netrc"
        netrc_file.write_text("default login anonymous password nya")
        parameters: list[str] = get_params(httpd.port, netrc=(str)(netrc_file))

        response = nix.nix(parameters).run().ok().stderr_plain

        assert f"Downloaded '{parameters[3]}' to '{nix.env.dirs.nix_store_dir}/" in response
        assert "-mrrp' (hash 'sha256-4uSfns8lpq5mZItbtpkOGcQjk7hqLjHF8OhrJt+n4Cw=')." in response


@with_files(ssl_files)
def test_fetch_wrong_netrc(nix: Nix, files: Path, app: web.Application) -> None:
    # Test that a wrong netrc file leads to unauthorized response
    with http_server(app) as httpd:
        netrc_file: Path = files / "empty-netrc"
        netrc_file.write_text("default login anonymous password nyaa")
        parameters: list[str] = get_params(httpd.port, netrc=(str)(netrc_file))

        error = nix.nix(parameters).run().expect(1).stderr_plain

        assert (
            f"error: unable to download '{parameters[3]}': HTTP error 401 (Unauthorized)" in error
        )
