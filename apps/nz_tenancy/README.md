# nz_tenancy — NZ residential tenancy Q&A service (Phase 6A skeleton)

The first real `apps/` binary. Phase 6A wires the HTTP spine end-to-end with
no business logic; Phase 6B will swap the echo / synthetic-stream paths for
the real `RAGPipeline` + `retrieve_anchor` + `Generator::generate_stream`
chain.

## Endpoints

| Method | Path          | Phase 6A behaviour                                    | Phase 6B target                          |
|--------|---------------|--------------------------------------------------------|------------------------------------------|
| GET    | `/health`     | `ok\n`                                                 | unchanged                                |
| POST   | `/ask`        | sanitize + JSON echo                                   | sanitize + RAG + non-streaming generate  |
| POST   | `/ask/stream` | sanitize + synthetic SSE (one word per chunk, 100 ms) | sanitize + RAG + streaming generate      |

## Build

```
cmake --preset prod
cmake --build --preset prod -t nz_tenancy
./build-prod/apps/nz_tenancy/nz_tenancy
```

For local dev (slower, sanitiser-instrumented; mimalloc verification is
auto-skipped because ASan installs its own allocator interceptors):

```
cmake --preset dev
cmake --build --preset dev -t nz_tenancy
./build-dev/apps/nz_tenancy/nz_tenancy
```

## Test from another terminal

```sh
curl http://localhost:8080/health

# Echo path
curl -X POST http://localhost:8080/ask \
     -H 'Content-Type: application/json' \
     -d '{"question":"what is the bond limit?"}'

# Streaming path (-N disables curl's output buffering)
curl -N -X POST http://localhost:8080/ask/stream \
     -H 'Content-Type: application/json' \
     -d '{"question":"my landlord refuses to fix the heating"}'
```

The stream path emits one `data: {"token":"..."}` line per word at ~100 ms
intervals, then closes with `data: [DONE]\n\n`. This is the same SSE shape
that Phase 6B will use for real LLM tokens.

## Config (env vars)

All `astraea::Config` env vars are honoured (see `include/astraea/config.hpp`).
Phase 6A reads:

| Variable | Default | Effect |
|----------|---------|--------|
| `PORT`   | 8080    | HTTP listener port |

Phase 6B will activate the rest (`LLM_BASE_URL`, `EMBED_BASE_URL`,
`QDRANT_URL`, etc.).

## mimalloc

Linked process-wide via `mimalloc_override.cpp` — a single TU that includes
`<mimalloc-new-delete.h>` so the global `operator new` / `operator delete`
replacements take effect. Verified at startup by `verify_mimalloc_override()`
in `main.cpp` (a `new int(42)` probe against `mi_is_in_heap_region()`);
aborts loudly with a diagnostic if the override is missing.

The verification is skipped under ASan because the sanitiser installs its
own allocator interceptors that take precedence. Production (Release) is
the only build that actually needs and gets the override.
