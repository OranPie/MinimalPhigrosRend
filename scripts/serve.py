#!/usr/bin/env python3
"""
Phigros Renderer — WASM preview server.

Serves the WASM build directory with correct MIME types and COOP/COEP headers
required for SharedArrayBuffer (used by Emscripten pthreads).

Usage:
    python3 scripts/serve.py                         # auto-detect build dir
    python3 scripts/serve.py --dir cpp/build_wasm    # explicit directory
    python3 scripts/serve.py --port 8080             # custom port
"""

from __future__ import annotations

import argparse
import http.server
import os
import socket
import sys
import threading
import webbrowser
from functools import partial
from pathlib import Path

# WASM build directory candidates (searched in order)
_BUILD_CANDIDATES = [
    "cpp/build_wasm",
    "cpp/build_wasm/bin",
    "cpp/build",
    "build_wasm",
]

MIME_OVERRIDES = {
    ".wasm": "application/wasm",
    ".js":   "application/javascript",
    ".mjs":  "application/javascript",
    ".json": "application/json",
    ".data": "application/octet-stream",
}


class WASMHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler with COOP/COEP headers and correct WASM MIME types."""

    def end_headers(self):
        # Required for SharedArrayBuffer (Emscripten pthreads)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def guess_type(self, path):
        ext = os.path.splitext(path)[1].lower()
        if ext in MIME_OVERRIDES:
            return MIME_OVERRIDES[ext]
        return super().guess_type(path)

    def log_message(self, format, *args):
        # Compact log output
        sys.stderr.write(f"  {args[0]}\n")


def find_build_dir(root: str) -> str | None:
    """Find the first existing WASM build directory."""
    for candidate in _BUILD_CANDIDATES:
        p = os.path.join(root, candidate)
        if os.path.isdir(p):
            # Check if it contains a .wasm or .html file
            for f in os.listdir(p):
                if f.endswith((".wasm", ".html", ".js")):
                    return p
    return None


def find_free_port(start: int = 8000, tries: int = 20) -> int:
    """Find a free port starting from `start`."""
    for port in range(start, start + tries):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.bind(("", port))
                return port
        except OSError:
            continue
    return start + tries


def main():
    parser = argparse.ArgumentParser(
        prog="serve",
        description="Phigros Renderer WASM preview server",
    )
    parser.add_argument(
        "--dir", "-d", type=str, default=None,
        help="Directory to serve (default: auto-detect WASM build)",
    )
    parser.add_argument(
        "--port", "-p", type=int, default=0,
        help="Port to serve on (default: auto-select from 8000+)",
    )
    parser.add_argument(
        "--no-open", action="store_true",
        help="Don't open browser automatically",
    )
    parser.add_argument(
        "--bind", "-b", type=str, default="localhost",
        help="Address to bind to (default: localhost)",
    )
    args = parser.parse_args()

    # Resolve serve directory
    repo_root = Path(__file__).resolve().parent.parent
    if args.dir:
        serve_dir = os.path.abspath(args.dir)
    else:
        serve_dir = find_build_dir(str(repo_root))
        if serve_dir is None:
            print("Error: No WASM build directory found.", file=sys.stderr)
            print("Build first:  cd cpp/build_wasm && emcmake cmake .. -DUSE_SDL3=OFF -DUSE_BGFX=OFF && emmake make", file=sys.stderr)
            print("Or specify:   python3 scripts/serve.py --dir <path>", file=sys.stderr)
            sys.exit(1)

    if not os.path.isdir(serve_dir):
        print(f"Error: Directory not found: {serve_dir}", file=sys.stderr)
        sys.exit(1)

    port = args.port or find_free_port()
    handler = partial(WASMHandler, directory=serve_dir)

    print(f"╭─ Phigros Renderer Preview Server ─────────────╮")
    print(f"│  Serving: {serve_dir}")
    print(f"│  URL:     http://{args.bind}:{port}/")
    print(f"╰────────────────────────────────────────────────╯")

    server = http.server.HTTPServer((args.bind, port), handler)

    if not args.no_open:
        # Open browser after a short delay
        url = f"http://{args.bind}:{port}/"
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
