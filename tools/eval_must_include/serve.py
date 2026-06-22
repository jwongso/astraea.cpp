#!/usr/bin/env python3
"""Tiny static-file server for the eval delta viewer.

Serves the static/ directory plus the sibling report.json / report.md at
the URL root. Also exposes two small live endpoints when an nz_tenancy
service is reachable:

  GET  /config   ->  {nz_url, llm_url, llm_name, probe_enabled, has_token}
  POST /probe    ->  proxy POST /ask/stream (with feedback_context:true),
                     streaming the SSE back to the browser verbatim, then
                     a final synthetic `probe_complete` event carrying the
                     server-computed actual + delta so the frontend can
                     render a live-vs-static comparison.

Stdlib only (the proxy imports `requests` because run_eval.py is required
anyway and the codepath shares its helpers). Designed to be run after
run_eval.py and either accessed locally or fronted by a Cloudflare tunnel.

Usage:
    python tools/eval_must_include/serve.py            # localhost:8765
    python tools/eval_must_include/serve.py --port 9000
"""
from __future__ import annotations

import argparse
import functools
import http.server
import json
import os
import pathlib
import socketserver
import sys
import webbrowser

# run_eval.py is the source of truth for upstream config, SSE parsing,
# actual-building, and delta math. We import lazily inside handlers so the
# server still starts even if requests isn't installed (the static view
# remains usable; only the probe endpoint will fail).
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


class _Handler(http.server.SimpleHTTPRequestHandler):
    """Serve files from static/, plus /report.json, /report.md, /config, /probe.

    Cache-Control policy:
      - report.json / report.md / config / probe: no-store (always fresh)
      - everything else (CSS/JS/HTML): max-age=60 so a Cloudflare edge can
        cache short-lived. Long max-ages don't make sense here because the
        report.json drives the page and changes per eval run.
    """

    # Used by end_headers to suppress the default cache header on dynamic paths.
    _DYNAMIC_PATHS = {"/report.json", "/report.json/",
                      "/report.md",   "/report.md/",
                      "/config",      "/config/",
                      "/probe",       "/probe/"}

    def __init__(self, *args, root: pathlib.Path, **kw):
        self._root = root
        super().__init__(*args, directory=str(root / "static"), **kw)

    def end_headers(self) -> None:                # noqa: D401
        if self.path not in self._DYNAMIC_PATHS:
            self.send_header("Cache-Control", "public, max-age=60")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Robots-Tag", "noindex, nofollow")
        super().end_headers()

    # ------------------------------------------------------------------
    # GET — static + dynamic JSON
    # ------------------------------------------------------------------
    def do_GET(self) -> None:                     # noqa: N802
        if self.path in ("/report.json", "/report.json/"):
            return self._send_file("report.json", "application/json; charset=utf-8")
        if self.path in ("/report.md", "/report.md/"):
            return self._send_file("report.md",   "text/markdown; charset=utf-8")
        if self.path in ("/config", "/config/"):
            return self._send_config()
        if self.path in ("/", ""):
            self.path = "/index.html"
        return super().do_GET()

    def _send_file(self, name: str, ctype: str) -> None:
        target = self._root / name
        if not target.exists():
            self.send_error(404, f"{name} not generated yet — run run_eval.py first")
            return
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type",  ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _send_config(self) -> None:
        """Frontend boot probe: tells the UI whether /probe will work."""
        cfg: dict[str, object] = {
            "nz_url":         os.environ.get("NZ_TENANCY_URL", "http://localhost:8001"),
            "has_token":      False,
            "probe_enabled":  False,
            "llm_name":       None,
            "llm_origin":     None,
        }
        try:
            import run_eval as R   # type: ignore[import-not-found]
            cfg["nz_url"]    = R.NZ_URL
            cfg["has_token"] = bool(R.NZ_TOKEN)
            cfg["probe_enabled"] = True
            # Best-effort: discover the model name once at config request time.
            try:
                name, origin = R._discover_llm_name(override=None)
                cfg["llm_name"]   = name
                cfg["llm_origin"] = origin
            except Exception:                     # noqa: BLE001
                pass
        except ImportError:
            pass
        body = json.dumps(cfg).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type",   "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control",  "no-store")
        self.end_headers()
        self.wfile.write(body)

    # ------------------------------------------------------------------
    # POST — live probe proxy
    # ------------------------------------------------------------------
    def do_POST(self) -> None:                    # noqa: N802
        if self.path in ("/probe", "/probe/"):
            return self._handle_probe()
        self.send_error(404)

    def _handle_probe(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            body   = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
            question = (body.get("question") or "").strip()
            golden   = body.get("golden") or {}
        except Exception as e:                    # noqa: BLE001
            self.send_error(400, f"bad request: {e}")
            return
        if not question:
            self.send_error(400, "question required")
            return

        try:
            import run_eval as R                  # type: ignore[import-not-found]
            import requests
        except ImportError as e:
            self.send_error(500, f"server-side dep missing: {e}")
            return

        headers = {
            "X-API-Key":    R.NZ_TOKEN,
            "Content-Type": "application/json",
            "X-No-Log":     "1",
        }
        payload = {"question": question, "feedback_context": True}
        upstream = f"{R.NZ_URL.rstrip('/')}/ask/stream"

        # Send SSE response headers first so the browser can start consuming
        # immediately. We use chunked-style writes via `self.wfile.flush()`
        # after each line — no Content-Length, the connection ends the stream.
        self.send_response(200)
        self.send_header("Content-Type",     "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control",    "no-store")
        self.send_header("X-Accel-Buffering", "no")
        self.end_headers()

        def _emit(line: bytes) -> bool:
            try:
                self.wfile.write(line)
                if not line.endswith(b"\n"):
                    self.wfile.write(b"\n")
                self.wfile.flush()
                return True
            except (BrokenPipeError, ConnectionResetError):
                return False

        def _emit_event(kind: str, data: dict) -> bool:
            payload = json.dumps({"type": kind, **data})
            return _emit(f"data: {payload}\n\n".encode("utf-8"))

        try:
            r = requests.post(upstream, json=payload, headers=headers,
                              stream=True, timeout=180)
        except requests.RequestException as e:
            _emit_event("probe_error", {"error": f"upstream connection failed: {e}"})
            return

        if r.status_code != 200:
            _emit_event("probe_error",
                        {"error": f"upstream HTTP {r.status_code}",
                         "body":  r.text[:400]})
            return

        # Forward each line verbatim, parsing for our own state along the way.
        tokens:        list[str]            = []
        sources:       dict | None          = None
        context_debug: dict | None          = None
        timing:        dict | None          = None
        current_event = ""

        for raw in r.iter_lines(decode_unicode=False):
            if raw is None:
                continue
            if not _emit(raw):
                return
            try:
                line = raw.decode("utf-8", errors="replace").strip()
            except Exception:                     # noqa: BLE001
                continue
            if not line:
                current_event = ""
                continue
            if line.startswith("event:"):
                current_event = line[6:].strip()
                continue
            if not line.startswith("data:"):
                continue
            ps = line[5:].lstrip()
            if ps == "[DONE]":
                break
            try:
                ev = json.loads(ps)
            except json.JSONDecodeError:
                continue
            if current_event == "sources" or ("sources" in ev and "type" not in ev):
                sources = ev
                current_event = ""
                continue
            kind = ev.get("type", "")
            if kind == "token":
                tokens.append(ev.get("text", ""))
            elif kind == "context_debug":
                context_debug = ev
            elif kind == "timing":
                timing = ev

        # After the upstream stream ends, run the same delta math run_eval.py
        # uses on disk so the live and static reports are directly comparable.
        try:
            stream_dict = {
                "answer":        "".join(tokens),
                "sources":       sources,
                "context_debug": context_debug,
                "timing":        timing,
            }
            actual = R._build_actual(stream_dict)
            delta  = R._delta(golden, actual) if golden else None
        except Exception as e:                    # noqa: BLE001
            _emit_event("probe_error",
                        {"error": f"server-side delta failed: {e}"})
            return

        _emit_event("probe_complete", {
            "actual":     actual,
            "delta":      delta,
            "token_count": len(tokens),
        })
        _emit(b"data: [DONE]\n")

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[serve] " + (fmt % args) + "\n")


class _ThreadingServer(socketserver.ThreadingMixIn,
                       socketserver.TCPServer):
    """Threaded so a long-lived /probe stream doesn't block /report.json."""
    allow_reuse_address = True
    daemon_threads      = True


def serve(port: int, root: pathlib.Path = HERE,
          open_browser: bool = True) -> None:
    handler = functools.partial(_Handler, root=root)
    with _ThreadingServer(("127.0.0.1", port), handler) as httpd:
        url = f"http://127.0.0.1:{port}/"
        print(f"[serve] http://127.0.0.1:{port}/  (Ctrl-C to stop)",
              file=sys.stderr)
        try:
            import run_eval as R  # noqa: F401  -- best-effort import for the log
            print(f"[serve] /probe enabled  ->  {os.environ.get('NZ_TENANCY_URL', 'http://localhost:8001')}",
                  file=sys.stderr)
        except ImportError:
            print("[serve] /probe disabled (requests / run_eval not importable)",
                  file=sys.stderr)
        if open_browser:
            try:
                webbrowser.open(url, new=2)
            except Exception:                     # noqa: BLE001
                pass
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[serve] stopped", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port",    type=int, default=8765)
    ap.add_argument("--no-open", action="store_true",
                    help="do not auto-open a browser")
    args = ap.parse_args()
    serve(args.port, open_browser=not args.no_open)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

