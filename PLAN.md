# astraea.cpp - C++ Port Plan

## Context

astraea (Python/FastAPI) is unstable on Gentoo: every host Python upgrade breaks
venvs, PyTorch races GPU allocation between processes, and Playwright downloads
browser binaries. Goal: a single binary per jurisdiction - no Python, no PyTorch,
no venv, no OS-upgrade surprises.

Source: `/home/wdha/proj/priv/astraea/` (~3,500 LOC, 20 modules)
Target repo: `https://github.com/jwongso/astraea.cpp`

---

## What Gets Dropped vs Ported

| Module           | Disposition    | Notes                                              |
|------------------|----------------|----------------------------------------------------|
| routing.py       | Full port      | Pure string logic, no deps                         |
| jurisdiction.py  | Full port      | Structs + abstract base                            |
| sanitize.py      | Full port      | Regex only                                         |
| embedder.py      | Replace        | HTTP call to llama-server /v1/embeddings           |
| retriever.py     | Full port      | Qdrant REST calls                                  |
| generator.py     | Full port      | llama-server /v1/chat/completions SSE              |
| reranker.py      | Replace        | HTTP call to llama-server /v1/rerank               |
| pipeline.py      | Full port      | Orchestration only                                 |
| anchor.py        | Full port      | Core retrieval logic                               |
| queue.py         | Full port      | Semaphore + per-IP atomic counters                 |
| session.py       | Full port      | Redis via hiredis                                  |
| feedback.py      | Full port      | JSONL writer + rotation                            |
| security.py      | Full port      | Drogon filter                                      |
| service.py       | Full port      | JurisdictionService                                |
| api.py           | Full port      | Drogon controller                                  |
| legislation.py   | Partial port   | Section extraction only, no browser fetch          |
| browser.py       | DROPPED        | Playwright - not viable in C++                     |
| web_verify.py    | DROPPED v1     | Needs headless browser - deferred                  |
| mcp.py           | DROPPED v1     | Deferred                                           |

**Embedder/Reranker**: llama.cpp already on this machine, supports `/v1/embeddings`
and `/v1/rerank`. Run a second llama-server instance on port 8081 serving
BAAI/bge-m3 GGUF (multilingual, good for German + NZ mixed corpus).
Zero PyTorch in production. C++ standard: C++23 (GCC 13+, Clang 16+).

---

## Tech Stack

| Concern       | Library           | Notes                                               |
|---------------|-------------------|-----------------------------------------------------|
| HTTP server   | Drogon            | Async, SSE, CORS, filters, static files             |
| HTTP client   | Drogon HttpClient | Async, connection pooling, keep-alive               |
| JSON          | nlohmann/json     | Header-only, explicit to/from_json converters       |
| Redis         | hiredis           | Async shim integrating with Drogon event loop       |
| Logging       | spdlog            | Async, structured                                   |
| Build         | CMake 3.25+ vcpkg | vcpkg.json manifest mode                            |
| Tests         | Catch2            | Unit tests for routing, sanitize                    |

No Boost. No Qt. C++23 (`std::expected`, `co_await`, `std::string_view`).

---

## Project Layout

```
astraea-cpp/
  CMakeLists.txt
  vcpkg.json
  include/astraea/
    config.hpp          # typed env var loading, constexpr defaults
    routing.hpp         # StatuteRoute, RouteDecision, normalize_query
    jurisdiction.hpp    # JurisdictionBase + all config structs
    sanitize.hpp        # input sanitization
    embedder.hpp        # REST client /v1/embeddings + synth cache
    retriever.hpp       # Qdrant REST client
    generator.hpp       # streaming LLM client
    reranker.hpp        # REST client /v1/rerank
    pipeline.hpp        # RAGPipeline
    anchor.hpp          # legislation anchor + federated search
    legislation.hpp     # section extraction cache (no browser)
    session.hpp         # Redis session
    feedback.hpp        # JSONL writer + rotation
    queue.hpp           # semaphore + per-IP rate limiter
    service.hpp         # JurisdictionService
    api.hpp             # Drogon app factory
  src/
    routing.cpp  sanitize.cpp  embedder.cpp  retriever.cpp
    generator.cpp  reranker.cpp  pipeline.cpp  anchor.cpp
    legislation.cpp  session.cpp  feedback.cpp  queue.cpp
    service.cpp  api.cpp  main.cpp
  jurisdictions/
    registry.cpp        # make_jurisdiction() factory
    nz_tenancy/         jurisdiction.hpp/cpp  routes.cpp
    flensburg/          jurisdiction.hpp/cpp  routes.cpp
    building_consents/  jurisdiction.hpp/cpp  routes.cpp
  tests/
    test_routing.cpp    # all Python route_fixture cases
    test_sanitize.cpp
    CMakeLists.txt
```

---

## Module Design

### config.hpp (header-only)

Typed struct loaded from `getenv`. Constexpr defaults. No dynamic string juggling
at request time. Passed everywhere by const-ref.

```cpp
struct Config {
    std::string llm_base_url       = "http://localhost:8080/v1";
    std::string embed_base_url     = "http://localhost:8081/v1";
    std::string qdrant_url         = "http://localhost:6333";
    std::string redis_url          = "redis://127.0.0.1:6379/0";
    std::string public_token;
    std::string debug_key;
    std::string allowed_origin     = "*";
    std::string llm_model          = "qwen3";
    int         llm_max_tokens     = 2500;
    float       llm_temperature    = 0.2f;
    int         llm_global_concurrency = 0;
    bool        enable_reranker    = true;

    static Config from_env();
};
```

### routing.hpp / routing.cpp

Direct port of routing.py. All types are value types (no heap allocation in hot
path). `normalize_query` is a single-pass UTF-8 scan: lowercase + fold umlaut
sequences (ue->u, oe->o, ae->a, ss->ss) + punctuation to space. No ICU needed.

```cpp
struct StatuteRoute {
    std::string intent;
    std::vector<std::string> include_any_precise;
    std::vector<std::string> include_any_broad;
    std::vector<std::string> require_context_any;
    std::vector<std::string> include_any;        // legacy flat-list
    std::vector<std::string> include_all;
    std::vector<std::string> exclude_any;
    std::vector<std::string> forced_sections;
    std::vector<std::string> leg_allow_list;
    std::vector<std::string> guidance_sources;
    std::string              synthetic_query;
    std::string              case_synthetic_query;
    int                      priority = 0;
};

struct RouteDecision {
    bool                                        triggered;
    std::vector<std::string>                    matched_intents;
    std::vector<std::string>                    trigger_terms;
    std::vector<std::pair<std::string,std::string>> trigger_paths;
    std::vector<std::string>                    forced_sections;
    std::vector<std::string>                    leg_allow_list;
    std::unordered_set<std::string>             boosted_act_ids;
    std::vector<std::string>                    leg_synthetic_queries;
    std::vector<std::string>                    case_synthetic_queries;
    std::string                                 dominant_route;
    std::string                                 dominance_reason;
    std::vector<std::pair<std::string,std::string>> ignored_routes;
    std::vector<std::pair<std::string,std::vector<std::string>>> near_miss_routes;
};

std::string   normalize_query(std::string_view text);
RouteDecision build_route_decision(std::string_view original,
                                   std::string_view rewritten,
                                   std::span<const StatuteRoute> routes);
```

Route matching: linear scan with `std::string::find` (SIMD-optimized in libc++).
Cache-local linear scan beats hash lookup for n < 100 routes.

### jurisdiction.hpp (header-only)

All config structs: `CorpusConfig`, `LegislationSource`, `ConfidenceConfig`,
`SmokeFixture`, `RouteFixture`. Abstract base class `JurisdictionBase` with pure
virtual `name()`, `corpus()`, `system_prompt()`, `routes()` and virtual defaults
for all optional overrides (same contract as Python).

### sanitize.hpp / sanitize.cpp

Injection patterns and address-only regex compiled once as `static const std::regex`
at program startup. Throws `SanitizeError` (caught by Drogon filter -> HTTP 400).
Direct port of Python logic including the LEGAL_TERMS_RE bypass.

### embedder.hpp / embedder.cpp

One persistent `drogon::HttpClientPtr` per embed server. POST to `/v1/embeddings`
with `{"model":"...","input":["text"]}`, parse `data[0].embedding`. Synthetic
query cache: `std::unordered_map<std::string, std::vector<float>>` guarded by
`std::shared_mutex` (many concurrent readers, rare writes at startup).

### retriever.hpp / retriever.cpp

Qdrant REST client. `Filter` struct serializes to Qdrant JSON filter format.
UUID5 point IDs via SHA-1 (openssl, already linked by Drogon).
One persistent `HttpClientPtr` per `VectorStore` instance.
Methods: `search`, `search_filtered`, `scroll_filtered`, `fetch_by_case_id`, `upsert`.

### generator.hpp / generator.cpp

Streams from `/v1/chat/completions`. Parses SSE `data: {...}` frames, extracts
`choices[0].delta.content`. Takes an `on_token` callback so the Drogon SSE
handler writes each token to the socket immediately - zero intermediate buffering.

### reranker.hpp / reranker.cpp

POST to `/v1/rerank`. Falls back gracefully (returns all candidates unfiltered)
when `ENABLE_RERANKER=false` or endpoint unavailable. `score_and_filter` preserves
the Python invariant: forced (route-injected) sections always pass regardless of score.

### pipeline.hpp / pipeline.cpp

Owns `Embedder`, `VectorStore`, `Generator` by value. `retrieve()` runs:
embed -> search -> `apply_manual_discounts` -> `deduplicate` -> `mmr_or_top_k` -> score filter.
`_mmr_select` uses word-overlap Jaccard, same algorithm as Python.
Manual source discount map: `static const std::unordered_map<std::string, float>`.

### anchor.hpp / anchor.cpp

`retrieve_anchor`, `retrieve_manual_guidance`, `augment_case_retrieval`,
`refine_retrieve` - direct ports of anchor.py. Parallel federated search via
`co_await drogon::when_all(per_source_searches...)`. Synthetic embed cache accessed
via `pipeline.embedder().embed_cached(synth_query)`.

### queue.hpp / queue.cpp

```cpp
class RequestQueue {
    std::atomic<int>                    _active{0};
    std::atomic<int>                    _waiting{0};
    std::mutex                          _ip_mu;
    std::unordered_map<std::string,int> _ip_in_flight;
    // Drogon coroutine-compatible semaphore
};
```

Global LLM Redis semaphore: atomic Lua `EVAL` via hiredis async, same Lua script
and TTL failsafe as the Python implementation.

### session.hpp / session.cpp

hiredis async `GET`/`SETEX`. Key: `astraea:session:<jurisdiction>:<session_id>`.
Serialized with nlohmann/json. Direct port: TTL sliding window, max 10 turns,
inject last 3 turns, cap answer at 400 chars, validate session_id regex.

### feedback.hpp / feedback.cpp

`std::ofstream` append, `std::mutex` per file, `std::filesystem::rename` for
rotation (keep last 5 rotations). Per-IP cooldown via
`std::unordered_map<std::string, std::chrono::steady_clock::time_point>` with mutex.
Files: `data/feedback.jsonl`, `data/feedback_full.jsonl`, `data/route_debug.jsonl`.

### api.hpp / api.cpp

Drogon `HttpFilter` subclasses registered at startup:
- `TokenAuthFilter` - checks X-API-Key header
- `BodySizeLimitFilter` - rejects bodies > 20 KB
- `SecurityHeadersFilter` - injects CSP, X-Frame-Options, Referrer-Policy
- `CorsFilter` - sets Access-Control headers from ALLOWED_ORIGIN

`POST /ask/stream` handler sequence:
1. Sanitize + preprocess question
2. `queue.acquire(ip)` (RAII ticket - auto-releases on scope exit)
3. `co_await drogon::when_all(retrieve, retrieve_anchor, retrieve_guidance)` - parallel
4. Inject guidance chunk, augment case retrieval, refine if low confidence
5. Build anchor string (session context + user context + legislation + web)
6. Emit `sources`, `confidence`, optional `context_debug` SSE frames
7. `global_llm_acquire(redis)` (RAII guard)
8. `generator.generate_stream(on_token)` - each token emitted as SSE `token` frame
9. Emit `done`
10. Save session to Redis, write route_debug.jsonl if enabled

### main.cpp

Reads `JURISDICTION` env var (or `--jurisdiction` CLI flag). Calls
`make_jurisdiction()` from `jurisdictions/registry.cpp`. Calls `create_app()`.
Starts Drogon on `127.0.0.1:<PORT>` with `std::thread::hardware_concurrency()` threads.

---

## Implementation Phases

**Phase 1 - Foundation** (compiles standalone, no network I/O)
- `CMakeLists.txt`, `vcpkg.json`
- `config.hpp`, `jurisdiction.hpp`
- `routing.hpp/cpp` - complete implementation
- `sanitize.hpp/cpp`
- `tests/test_routing.cpp` - all Python `route_fixture` cases ported
- `tests/test_sanitize.cpp`

Deliverable: `./build/tests/test_routing` and `test_sanitize` pass.

**Phase 2 - External clients**
- `embedder.hpp/cpp` - `/v1/embeddings` client
- `retriever.hpp/cpp` - Qdrant REST
- `generator.hpp/cpp` - streaming `/v1/chat/completions`
- `reranker.hpp/cpp` - `/v1/rerank` with fallback

Deliverable: each client has a `--smoke` test mode hitting live services.

**Phase 3 - Pipeline + Anchor**
- `pipeline.hpp/cpp`
- `anchor.hpp/cpp`
- `legislation.hpp/cpp`

Deliverable: CLI binary `astraea-retrieve --question "..." --jurisdiction flensburg`
prints top-5 sources + legislation anchor to stdout.

**Phase 4 - Server infrastructure**
- `queue.hpp/cpp`
- `session.hpp/cpp`
- `feedback.hpp/cpp`
- `service.hpp/cpp`

**Phase 5 - HTTP API + main**
- `api.hpp/cpp` (all endpoints, SSE, filters, static files)
- `main.cpp`, `jurisdictions/registry.cpp`

Deliverable: `JURISDICTION=flensburg PORT=8004 ./build/astraea` serves all endpoints.

**Phase 6 - Jurisdiction implementations**
- `jurisdictions/flensburg/` - full routes table ported from `routes.py`
- `jurisdictions/nz_tenancy/`
- `jurisdictions/building_consents/`

**Phase 7 - Deployment + CI**
- systemd service units replacing Python units (one per jurisdiction)
- llama-server embed unit on port 8081 (BAAI/bge-m3-GGUF)
- GitHub Actions: build + Catch2 tests on ubuntu-latest

---

## Performance Design

1. **Async throughout**: Drogon C++20 coroutines (`co_await`) for all I/O.
   No blocking calls on event loop threads. Thread count = `hardware_concurrency`.

2. **Connection pools**: One persistent `drogon::HttpClientPtr` per upstream
   (Qdrant, LLM gen, LLM embed). HTTP keep-alive eliminates TCP handshake cost.

3. **Zero-copy SSE**: Drogon `AsyncStreamResponse` writes tokens directly to the
   socket buffer as they arrive from the LLM. No intermediate accumulation.

4. **Embed cache**: Synthetic query vectors computed once at startup, stored in
   `Embedder` with `std::shared_mutex` (concurrent reads, write only at init).

5. **Parallel retrieval**: Corpus + legislation + guidance fetched concurrently
   via `co_await drogon::when_all(...)`, same as Python `asyncio.gather`.

6. **Route matching**: Single `normalize_query` call per request. Linear scan
   over `std::span<const StatuteRoute>` with `std::string::find` (SIMD in libc++).
   Cache-local for n < 100 routes.

7. **Compiler flags**: `-O2 -march=native` in Release.
   `-fsanitize=address,undefined` in Debug.

---

## Verification

After Phase 5:
- Start embed llama-server: `llama-server --model bge-m3.gguf --embedding --port 8081`
- `JURISDICTION=flensburg PORT=8004 ./build/astraea`
- Run existing `run_batch.py` against C++ server; compare route breakdown + gap counts
  with Python baseline (target: same or better hard routing %)
- `hey -n 100 -c 5 http://localhost:8004/health` - verify no crashes under load
- p99 latency target for `/ask/stream` first-token: <300 ms (vs ~800 ms Python)
- Swap systemd units one jurisdiction at a time; verify via Cloudflare tunnel
