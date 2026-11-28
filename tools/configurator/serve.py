#!/usr/bin/env python3
"""Local development HTTP server for Tandem Configurator.

Serves tools/configurator/ on http://localhost:<port> (a secure context for
WebSerial and Web Bluetooth in Chrome/Edge) without requiring any build step.
"""

import argparse
import functools
import http.server
import pathlib
import socketserver


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Serve tools/configurator/ locally for WebSerial/Web Bluetooth testing."
    )
    parser.add_argument(
        "-p", "--port", type=int, default=8000, help="Port to listen on (default: 8000)"
    )
    args = parser.parse_args()

    web_dir = pathlib.Path(__file__).resolve().parent
    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=str(web_dir)
    )

    class ReusableTCPServer(socketserver.TCPServer):
        allow_reuse_address = True

    with ReusableTCPServer(("127.0.0.1", args.port), handler) as httpd:
        url = f"http://localhost:{args.port}/"
        print(f"Serving Tandem Configurator from {web_dir}")
        print(f"Open in Chrome/Edge: {url}")
        print("Press Ctrl+C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")


if __name__ == "__main__":
    main()
