# nz_tenancy -- NZ residential tenancy Q&A service

Single-binary RAG server for New Zealand tenancy law. Retrieves relevant
case decisions and legislation from Qdrant, reranks them, builds LLM context,
and streams the answer via SSE. Session history is kept in Redis across turns.

## Endpoints

| Method | Path           | Description                                           |
|--------|----------------|-------------------------------------------------------|
| GET    | `/health`      | Liveness probe -- returns `ok\n`                      |
| GET    | `/healthz`     | Readiness probe -- pings Qdrant, LLM, embed, rerank   |
| POST   | `/ask`         | RAG + non-streaming JSON answer                       |
| POST   | `/ask/stream`  | RAG + SSE token stream with timing event              |
| POST   | `/feedback`    | User rating (1-5 stars) written to feedback.jsonl     |

All POST routes also accept OPTIONS for CORS preflight.

### /ask request

```json
{"question": "What is the bond limit for a residential tenancy?"}
```

Optional header: `X-Session-Id: <uuid>` to maintain conversation history.

### /ask/stream SSE format

```
event: sources
data: {"sources":[{"id":"...","score":0.92,"label":"..."}],"guidance_source":null}

data: {"token":"The"}
data: {"token":" bond"}
...
data: {"type":"timing","rewrite_ms":45.2,"retrieve_ms":120.0,...}
data: [DONE]
```

The `timing` event is emitted when built with `-DASTRAEA_ENABLE_TIMING` (the
default). Named slots: `rewrite_ms`, `retrieve_ms`, `anchor_ms`, `guidance_ms`,
`context_ms`, `llm_wait_ms`, `ttft_ms`, `generation_ms`, `total_ms`.

### /feedback request

```json
{"question": "What is the bond limit?", "rating": 5, "comment": "Very helpful"}
```

`rating` is required (1-5). `comment` is optional. Rate-limited to one
submission per IP per 30 seconds.

## Build

```sh
cmake --preset prod
cmake --build --preset prod -t nz_tenancy
./build-prod/apps/nz_tenancy/nz_tenancy
```

Dev build (ASan + UBSan instrumented, mimalloc verification skipped):

```sh
cmake --preset dev
cmake --build --preset dev -t nz_tenancy
./build-dev/apps/nz_tenancy/nz_tenancy
```

Disable compile-time timing (removes the `timing` SSE event):

```sh
cmake --preset prod -DASTRAEA_ENABLE_TIMING=OFF
```

## Config (env vars)

| Variable                 | Default                         | Description                                            |
|--------------------------|---------------------------------|--------------------------------------------------------|
| `PORT`                   | `8080`                          | HTTP listener port                                     |
| `THREAD_COUNT`           | `hardware_concurrency`          | Drogon I/O threads (0 = auto-detect)                   |
| `MAX_BODY_BYTES`         | `16384`                         | Max request body size in bytes                         |
| `LLM_BASE_URL`           | `http://localhost:8080/v1`      | Chat LLM server                                        |
| `LLM_MODEL`              | `qwen3`                         | Chat model name                                        |
| `LLM_MAX_TOKENS`         | `2500`                          | Max tokens for generation                              |
| `LLM_TEMPERATURE`        | `0.2`                           | Generation temperature                                 |
| `LLM_GLOBAL_CONCURRENCY` | `0`                             | Global LLM permit cap (0 = unlimited)                  |
| `LLM_ACQUIRE_TIMEOUT_S`  | `90`                            | Seconds to wait for LLM permit before 503              |
| `COORDINATOR_BACKEND`    | `in_process`                    | `in_process` or `redis`                                |
| `REWRITE_MAX_TOKENS`     | `100`                           | Max tokens for query rewrite LLM call                  |
| `REWRITE_TEMPERATURE`    | `0.0`                           | Temperature for query rewrite (deterministic)          |
| `EMBED_BASE_URL`         | `http://localhost:8081/v1`      | Embedding server                                       |
| `EMBED_MODEL`            | `BAAI/bge-m3`                   | Embedding model name                                   |
| `EMBED_DIMS`             | `1024`                          | Embedding dimension (must match Qdrant collection)     |
| `RERANK_BASE_URL`        | `http://localhost:8081/v1`      | Reranker server (defaults to embed server)             |
| `RERANK_MODEL`           | `BAAI/bge-m3`                   | Reranker model name                                    |
| `ENABLE_RERANKER`        | `true`                          | Set `false` to skip reranking step                     |
| `ENABLE_THINKING`        | `true`                          | Forward `enable_thinking` for Qwen3 (set false for other models) |
| `QDRANT_URL`             | `http://localhost:6333`         | Qdrant vector database                                 |
| `REDIS_URL`              | `redis://127.0.0.1:6379/0`      | Redis for session history (empty = disabled)           |
| `SESSION_TTL_S`          | `3600`                          | Session TTL in seconds                                 |
| `SESSION_MAX_TURNS`      | `10`                            | Max user+assistant turn pairs per session              |
| `FEEDBACK_DIR`           | `data`                          | Directory for JSONL log files                          |
| `FEEDBACK_MAX_MB`        | `20`                            | Per-file rotation limit for question_log + feedback    |
| `ROUTE_DEBUG_MAX_MB`     | `50`                            | Per-file rotation limit for route_debug                |
| `IP_MAX_CONCURRENCY`     | `3`                             | Max concurrent requests per IP (0 = unlimited)         |
| `PUBLIC_TOKEN`           | _(empty)_                       | Required `X-API-Key` value (empty = auth disabled)     |
| `DEBUG_KEY`              | _(empty)_                       | Key for debug-only endpoints                           |
| `ALLOWED_ORIGIN`         | `*`                             | `Access-Control-Allow-Origin` header value             |

## JSONL logs

Written to `FEEDBACK_DIR` (default `data/`):

| File               | Written when        | Content                                     |
|--------------------|---------------------|---------------------------------------------|
| `question_log.jsonl` | Every question (no `X-No-Log`) | `{ts, q}` |
| `route_debug.jsonl`  | Same              | `{ts, q, rewritten, triggered, matched_intents, sources, legislation, answer}` |
| `feedback.jsonl`     | POST /feedback    | `{ts, question, rating, comment}` |

Add `X-No-Log: 1` to any request to suppress question_log and route_debug writes
(used by automated tests and benchmarks).

Files rotate when they exceed their size limit. Up to 5 rotated copies are kept
(`.1` is the most recent archive).

## mimalloc

Linked process-wide via `mimalloc_override.cpp` -- a single TU that includes
`<mimalloc-new-delete.h>` to replace the global `operator new` / `operator delete`.
Verified at startup by a `new int(42)` probe against `mi_is_in_heap_region()`.
Aborts loudly if the override is not active. Skipped under ASan (the sanitiser
installs its own interceptors that take precedence).
