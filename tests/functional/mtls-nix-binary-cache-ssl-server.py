#!/usr/bin/env python3
import http.server
import ssl
import socketserver
import sys
import os
import argparse
import textwrap
from typing import Any

class NixCacheHandler(http.server.BaseHTTPRequestHandler):
    protocol_version: str = 'HTTP/1.1'

    def do_GET(self) -> None:
        # Get client certificate information
        try:
            client_cert: dict[str, Any] | None = self.request.getpeercert()
        except Exception as e:
            print(f"Error getting client certificate: {e}", file=sys.stderr)
            self.send_error(403, "Invalid client certificate")
            return

        if not client_cert:
            self.send_error(403, "No client certificate provided")
            return

        # Additional validation - check if certificate chain is valid
        subject: tuple[tuple[tuple[str, str], ...], ...] | None = client_cert.get('subject')
        if not subject:
            self.send_error(403, "Invalid client certificate: No subject")
            return

        # Log client info
        print(f"Client connected: {subject}", file=sys.stderr)
        print(f"Path requested: {self.path}", file=sys.stderr)

        # Handle nix-cache-info endpoint
        if self.path == '/nix-cache-info':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Connection', 'close')  # Explicitly close after response
            test_root: str | None = os.environ.get('TEST_ROOT')
            if not test_root:
                store_root: str = '/nix/store'
            else:
                store_root = os.path.join(test_root, 'store')

            # Nix cache info format
            cache_info: str = textwrap.dedent(f"""\
                StoreDir: {store_root}
                WantMassQuery: 1
                Priority: 30
            """)
            self.send_header('Content-Length', str(len(cache_info)))
            self.end_headers()
            self.wfile.write(cache_info.encode())
            self.wfile.flush()  # Ensure data is sent

        # Handle .narinfo requests
        elif self.path.endswith('.narinfo'):
            # Return 404 for all narinfo requests (empty cache)
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.send_header('Connection', 'close')
            self.end_headers()

        else:
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.send_header('Connection', 'close')
            self.end_headers()

    def log_message(self, format: str, *args: Any) -> None:
        # Suppress standard logging
        pass

def run_server(port_fifo_path: str, certfile: str, keyfile: str, ca_certfile: str) -> None:
    # Create SSL context
    context: ssl.SSLContext = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    context.load_cert_chain(certfile=certfile, keyfile=keyfile)
    context.verify_mode = ssl.VerifyMode.CERT_REQUIRED
    context.check_hostname = False  # We're not checking hostnames for client certs
    context.load_verify_locations(cafile=ca_certfile)

    # Bind to a free port
    with socketserver.TCPServer(("localhost", 0), NixCacheHandler) as httpd:
        port = httpd.server_address[1]  # Extract chosen port

        # Wrap with TLS
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)

        # Write the port to the FIFO
        with open(port_fifo_path, 'w') as fifo:
            fifo.write(f"{port}\n")
            fifo.flush()

        print(f"Server running on port {port}", file=sys.stderr)

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            httpd.shutdown()

if __name__ == "__main__":
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description='Nix binary cache server with SSL client verification')
    parser.add_argument('--port-fifo', type=str, required=True, help="FIFO where to inform about the port taken")
    parser.add_argument('--cert', required=True, help='Server certificate file')
    parser.add_argument('--key', required=True, help='Server private key file')
    parser.add_argument('--ca-cert', required=True, help='CA certificate for client verification')

    args: argparse.Namespace = parser.parse_args()

    run_server(args.port_fifo, args.cert, args.key, args.ca_cert)
