"""
HTTP server fixture for tests which binds to an auto-assigned port on localhost.
"""

import asyncio
import contextlib
import dataclasses
import logging
import time
import socket
import threading
from queue import Queue
import aiohttp.web as web
import aiohttp.http_writer as http_writer

logger = logging.getLogger(__name__)


@dataclasses.dataclass
class HttpServer:
    app: web.Application
    port: int


class EventTS(asyncio.Event):
    """
    A thread safe version of the asyncio Event

    NOTE: clear() is not thread safe

    Taken from https://stackoverflow.com/a/33006667
    """

    def __init__(self, *args, loop: asyncio.AbstractEventLoop | None = None, **kwargs):
        """
        Creates a thread-safe event for the given loop (or the loop of the current thread).
        """
        super().__init__(*args, **kwargs)
        self.target_loop = loop or asyncio.get_running_loop()

    def set(self):
        self.target_loop.call_soon_threadsafe(super().set)


def _make_localhost_socket(port: int = 0) -> tuple[socket.socket, int]:
    """Creates a localhost-bound socket with an auto-assigned port (unless specified)."""
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    # Shouldn't matter because we dynamically allocate ports, but this is generally preferred.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("::1", port))
    _, port = sock.getsockname()[:2]

    return (sock, port)


def _server_thread(app: web.Application, sock: socket.socket, shutdown_ev_q: Queue):
    async def async_main():
        nonlocal app, sock
        # Due to Reasons(tm) of event loop lifecycles and stuff of the sort,
        # it's far easier to just send the event object to the other thread
        # from inside the loop where it already knows which loop it is.
        shutdown_ev = EventTS()
        shutdown_ev_q.put(shutdown_ev)

        runner = web.AppRunner(app, handle_signals=False)
        await runner.setup()
        site = web.SockSite(runner, sock)
        await site.start()
        await shutdown_ev.wait()
        await runner.cleanup()

    asyncio.run(async_main())


@contextlib.contextmanager
def http_server(app: web.Application, port: int = 0):
    """
    Creates an http server on an automatically chosen port (if not given) on
    the host running the given web.Application, gives you the port for it.

    The server is run on a separate thread.
    """
    # n.b. pytest doesn't directly support asyncio. There's a bunch of
    # complexity that we could go through to do this or we could just throw the
    # async on a thread which was what we would do to the web server anyway if
    # it was blocking.
    shutdown_ev_q = Queue()
    thr = None
    sock = None
    shutdown_ev = None
    try:
        sock, port = _make_localhost_socket(port=port)
        thr = threading.Thread(
            target=_server_thread,
            args=(app, sock, shutdown_ev_q),
            name=f"functional2 httpd [::1]:{port}",
        )
        thr.start()
        shutdown_ev = shutdown_ev_q.get()
        yield HttpServer(app=app, port=port)
    finally:
        if shutdown_ev:
            shutdown_ev.set()
        if thr:
            thr.join()
        if sock:
            sock.close()


def dev_main():
    """A little test server for poking at this manually"""
    logging.basicConfig(level=logging.DEBUG)

    async def root(_req: web.Request) -> web.Response:
        # sadly required to make this function async
        await asyncio.sleep(0.01)
        return web.Response(body="hello world")

    async def eof(req: web.Request) -> web.StreamResponse:
        # sadly required to make this function async
        await asyncio.sleep(0.01)
        writer: http_writer.StreamWriter = req.writer  # type: ignore
        assert writer.transport
        writer.transport.close()
        return web.StreamResponse()

    async def eof_after_headers(req: web.Request) -> web.StreamResponse:
        resp = web.StreamResponse()
        resp.headers["content-length"] = "1337"
        await resp.prepare(req)
        await resp.write_eof()
        writer: http_writer.StreamWriter = req.writer  # type: ignore
        assert writer.transport
        writer.transport.close()
        return resp

    app = web.Application()
    app.add_routes(
        [web.get("/", root), web.get("/eof", eof), web.get("/eof_after_headers", eof_after_headers)]
    )

    with http_server(app, 1337) as httpd:
        logger.info("Listening on http://[::1]:%d", httpd.port)
        while True:
            time.sleep(3600)


if __name__ == "__main__":
    dev_main()
