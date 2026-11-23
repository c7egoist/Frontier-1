#!/usr/bin/env python3
"""Serve the standalone AO workbench on 0.0.0.0 for local/preview use."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]
DEMO_PATH = "/Experimental/ShadowAlgorithmLab/index.html"


class AoWorkbenchHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(REPO_ROOT), **kwargs)

    def do_GET(self):
        if self.path.split("?", 1)[0] in ("/", "/index.html", "/ao"):
            self.send_response(302)
            self.send_header("Location", DEMO_PATH)
            self.end_headers()
            return
        super().do_GET()

    def do_HEAD(self):
        if self.path.split("?", 1)[0] in ("/", "/index.html", "/ao"):
            self.send_response(302)
            self.send_header("Location", DEMO_PATH)
            self.end_headers()
            return
        super().do_HEAD()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8098
    server = ThreadingHTTPServer(("0.0.0.0", port), partial(AoWorkbenchHandler))
    print(f"AO Workbench serving on http://0.0.0.0:{port}/", flush=True)
    server.serve_forever()
