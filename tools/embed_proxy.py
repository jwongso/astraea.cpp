#!/usr/bin/env python3
"""
Minimal OpenAI-compatible /v1/embeddings proxy for nomic-embed-text-v1.5.

Applies "search_query: " prefix to all inputs so the C++ astraea embedder
produces vectors compatible with the nztt_moj Qdrant collection (which was
ingested with sentence_transformers using the same prefix convention).

Usage:
    /path/to/venv/bin/python embed_proxy.py [--port 8081] [--model MODEL]
"""

import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

QUERY_PREFIX = "search_query: "


def load_model(model_name: str):
    from sentence_transformers import SentenceTransformer
    print("Loading %s ..." % model_name, flush=True)
    t0 = time.time()
    m = SentenceTransformer(model_name, trust_remote_code=True)
    print("Model loaded in %.1fs, dim=%d" % (time.time() - t0, m.get_sentence_embedding_dimension()), flush=True)
    return m


MODEL = None
MODEL_NAME = "nomic-ai/nomic-embed-text-v1.5"


class EmbedHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # suppress access log spam

    def do_GET(self):
        if self.path == "/health":
            self._json(200, {"status": "ok"})
        elif self.path.startswith("/v1/models"):
            self._json(200, {"object": "list", "data": [
                {"id": MODEL_NAME, "object": "model"}
            ]})
        else:
            self._json(404, {"error": "not found"})

    def do_POST(self):
        if not self.path.startswith("/v1/embeddings"):
            self._json(404, {"error": "not found"})
            return

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        try:
            req = json.loads(body)
        except Exception:
            self._json(400, {"error": "invalid JSON"})
            return

        raw = req.get("input", "")
        texts = raw if isinstance(raw, list) else [raw]
        prefixed = [QUERY_PREFIX + t for t in texts]

        t0 = time.time()
        vecs = MODEL.encode(prefixed, normalize_embeddings=True, show_progress_bar=False).tolist()
        elapsed = time.time() - t0

        data = [
            {"object": "embedding", "index": i, "embedding": v}
            for i, v in enumerate(vecs)
        ]
        self._json(200, {
            "object": "list",
            "model": MODEL_NAME,
            "data": data,
            "usage": {"prompt_tokens": sum(len(t.split()) for t in texts), "total_tokens": 0},
            "_elapsed_s": round(elapsed, 3),
        })

    def _json(self, code: int, obj: dict):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    global MODEL, MODEL_NAME
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8081)
    p.add_argument("--model", default="nomic-ai/nomic-embed-text-v1.5")
    args = p.parse_args()
    MODEL_NAME = args.model
    MODEL = load_model(MODEL_NAME)
    server = HTTPServer(("127.0.0.1", args.port), EmbedHandler)
    print("Embed proxy listening on http://127.0.0.1:%d" % args.port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Shutting down.", flush=True)


if __name__ == "__main__":
    main()
