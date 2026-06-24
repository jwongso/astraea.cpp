# MCP Server - astraea.cpp

Every jurisdiction binary ships a built-in MCP (Model Context Protocol) server
at `POST /mcp`. It runs the Streamable-HTTP transport in stateless mode: each
request is a self-contained JSON-RPC 2.0 call with no session state on the
server. No separate process or SDK is needed - the endpoint is part of the same
Drogon binary as `/ask/stream`.

## Transport

| Property | Value |
|---|---|
| Transport | MCP Streamable-HTTP (stateless) |
| Endpoint | `POST /mcp` |
| Auth | `X-API-Key: <PUBLIC_TOKEN>` header (same token as `/ask`) |
| Content-Type | `application/json` |
| Protocol | JSON-RPC 2.0 |

OPTIONS `/mcp` returns 200 with CORS headers for browser preflight.

## Supported methods

| Method | Description |
|---|---|
| `initialize` | Handshake - returns server capabilities and `protocolVersion` |
| `notifications/initialized` | Client confirmation (fire-and-forget, returns 204) |
| `tools/list` | Enumerate the 4 legal tools with JSON Schema |
| `tools/call` | Invoke a named tool with arguments |

## Tools

### `legal_search`

Vector-similarity search over Tribunal decisions and legislation. Returns scored
source documents. Use this when you want raw citations to hand to a human or to
a generation step you control.

**Arguments:**

| Field | Type | Required | Description |
|---|---|---|---|
| `query` | string | yes | Legal question or topic to search for |
| `top_k` | integer | no | Results to return (1-20, default 5) |

**Returns** (as JSON string in `content[0].text`):

```json
{
  "count": 3,
  "sources": [
    {
      "score": "0.823",
      "title": "Smith v Jones [2023] NZTT Wellington 4539",
      "case_id": "TT-2023-WN-4539",
      "url": "https://...",
      "date": "2023-04-15",
      "court_name": "Tenancy Tribunal",
      "text": "..."
    }
  ]
}
```

---

### `legal_ask`

Full RAG pipeline: embeds the question, retrieves relevant sources, then
generates a complete answer using the jurisdiction's system prompt. Uses
non-streaming generation so the tool response is a single JSON object.

Use this when you want a synthesised legal explanation rather than raw sources.
The model behind the generation is Qwen3-8B (or whatever `LLM_MODEL` is
configured to) running at `LLM_BASE_URL`.

**Arguments:**

| Field | Type | Required | Description |
|---|---|---|---|
| `question` | string | yes | The legal question to answer |

**Returns** (as JSON string in `content[0].text`):

```json
{
  "answer": "A landlord may deduct from the bond for damage under s105...",
  "sources": [ { "score": "0.85", "title": "...", ... } ]
}
```

---

### `legal_get_source`

Fetch a single Tribunal decision or corpus document by its Qdrant UUID
(`source_id`). The UUID is returned in the `id` field of `legal_search` results.

**Arguments:**

| Field | Type | Required | Description |
|---|---|---|---|
| `source_id` | string | yes | UUID of the Qdrant corpus point |

**Returns** (as JSON string in `content[0].text`):

Full payload object with all stored fields (`title`, `case_id`, `url`, `date`,
`court_name`, `text`, `chunk_index`, ...).

---

### `legal_get_legislation`

Fetch the operative text of an NZ legislation section by its `case_id`
(e.g. `NZLEG/RTA/s42A`, `NZLEG/HHS/r8`). Returns the chunk with the lowest
`chunk_index` (the start of the section), selected from the `nz_legal_v2`
Qdrant collection.

**Arguments:**

| Field | Type | Required | Description |
|---|---|---|---|
| `section_id` | string | yes | case_id of the section (e.g. `NZLEG/RTA/s49B`) |

**Returns** (as JSON string in `content[0].text`):

```json
{
  "case_id": "NZLEG/RTA/s49B",
  "title": "s49B When tenant liable",
  "text": "s49B When tenant liable\n\n(1) A tenant is not excused...",
  "chunk_index": "0",
  "url": "https://www.legislation.govt.nz/act/public/1986/0120/latest/",
  "score": "0"
}
```

---

## Full request/response examples

### Initialize

```bash
curl -s -X POST http://localhost:8001/mcp \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $TOKEN" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2024-11-05",
      "capabilities": {},
      "clientInfo": { "name": "hyni", "version": "1.0" }
    }
  }'
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": { "tools": {} },
    "serverInfo": { "name": "astraea-nz-tenancy", "version": "1.0.0" }
  }
}
```

### tools/call - legal_search

```bash
curl -s -X POST http://localhost:8001/mcp \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $TOKEN" \
  -d '{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/call",
    "params": {
      "name": "legal_search",
      "arguments": { "query": "entry notice 24 hours", "top_k": 3 }
    }
  }'
```

### tools/call - legal_get_legislation

```bash
curl -s -X POST http://localhost:8001/mcp \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $TOKEN" \
  -d '{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "legal_get_legislation",
      "arguments": { "section_id": "NZLEG/RTA/s48" }
    }
  }'
```

## Integration with hyni.web

hyni.web has a stdio MCP client (`src/hyni/mcp_client.h`). To connect it to
the astraea.cpp HTTP MCP endpoint, add HTTP-mode support to `mcp_client` using
the `server_spec` struct with a `transport = "http"` field, then send JSON-RPC
2.0 requests directly to `POST http://localhost:8001/mcp` with the API key in
the `X-API-Key` header.

Alternatively, add a thin stdio-to-HTTP adapter script:

```python
# mcp_http_proxy.py - proxies stdio JSON-RPC to POST /mcp
import sys, json, urllib.request, os

BASE = os.environ.get("ASTRAEA_URL", "http://localhost:8001")
TOKEN = os.environ.get("ASTRAEA_TOKEN", "")

for line in sys.stdin:
    req = json.loads(line)
    body = json.dumps(req).encode()
    http_req = urllib.request.Request(
        BASE + "/mcp", data=body,
        headers={"Content-Type": "application/json", "X-API-Key": TOKEN},
        method="POST")
    with urllib.request.urlopen(http_req, timeout=60) as r:
        sys.stdout.write(r.read().decode() + "\n")
        sys.stdout.flush()
```

Then in `.env`:

```
HYNI_MCP_SERVERS=nz-legal|python3|/path/to/mcp_http_proxy.py||ASTRAEA_URL=http://localhost:8001,ASTRAEA_TOKEN=<token>
```

The preferred long-term path is to add native HTTP transport to `mcp_client`
so no Python proxy process is needed.

## Implementation notes

- **Stateless**: no MCP session tokens, no `Mcp-Session-Id` headers. Each
  request is independent. This matches FastMCP's `stateless_http: True` mode.
- **Auth**: `X-API-Key` goes through the same `CRYPTO_memcmp` constant-time
  check as all other endpoints. OPTIONS is exempt from auth (CORS preflight).
- **`legal_ask` generation**: uses `Generator::generate()` (non-streaming,
  same path as query rewrite). The LLM global concurrency semaphore is NOT
  acquired for MCP tool calls - they bypass the per-IP and global LLM limits.
  This is intentional: tool calls originate from the backend, not from
  end-user sessions, and should not compete with live `/ask/stream` traffic.
  If contention becomes an issue, wire `llm_sem->acquire()` around the
  `generator.generate()` call in `tool_legal_ask`.
- **`legal_get_legislation`**: uses Qdrant's `/points/scroll` endpoint
  (via `VectorStore::scroll_by_filter`) rather than vector search, because
  `case_id` is a payload field - not the Qdrant UUID point ID. The `fetch()`
  method requires UUID IDs and cannot be used for `case_id` lookups.
- **Source file**: `apps/nz_tenancy/mcp_handler.hpp` / `mcp_handler.cpp`.
  The `register_mcp_handler()` function is called from `main.cpp` after all
  other routes are registered.
