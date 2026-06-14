# Astraea C++ vs Python - End-to-End Timing Benchmark

All runs hit the same host (Gentoo Linux, Mac Mini M4 Pro 48GB), same LLM backend
(Qwen3-8B-Q5_K_M on port 8080), same Qdrant instance (port 6333), n=5 questions each.
All requests include `X-No-Log: 1`.

Python baseline file: `.training/benchmark_baseline.json`
C++ v5 results file: `/tmp/cpp_benchmark_v5.json`
Benchmark script: `.training/benchmark_timing.py`

---

## Results (mean, n=5)

| Slot              | Python (ms) | C++ v1 (ms) | C++ v5 (ms) | vs Python    |
|-------------------|-------------|-------------|-------------|--------------|
| sanitize_ms       | ~0.0 (*)    | 0.1         | 0.1         | --           |
| route_ms          | 0.5         | n/a (+)     | n/a (+)     | --           |
| embed_ms          | 273.2       | 12.2        | 5.8         | **47x faster** |
| anchor_ms         | 119.7       | 35.2        | 23.5        | **5.1x faster** |
| guidance_ms       | 57.4        | 10.7        | 10.7        | **5.4x faster** |
| context_assembly  | 1.9         | n/a (+)     | n/a (+)     | --           |
| llm_wait_ms       | ~0.001      | ~0.0        | ~0.0        | same         |
| ttft_ms           | 1,429       | 12,717      | 714         | **2x faster** |
| generation_ms     | 11,325      | 24,491      | 9,927       | 1.1x faster  |
| total_ms          | 12,727      | 25,305      | 10,709      | **1.2x faster** |

(*) Python `sanitize_ms` was not instrumented in the baseline run (always 0).
(+) These sub-slots are not emitted individually by the C++ timing event. They
are folded into the parent slot (embed_ms covers the full embed HTTP round-trip;
route decision and context assembly are sub-microsecond and not separately timed).

C++ v1 = 2026-06-14, thinking=on (run script bug)
C++ v5 = 2026-06-15, thinking=off (fixed)

---

## Retrieval Phase - C++ Wins

The three parallel tasks (embed+retrieve, anchor, guidance) run as coroutines
via `drogon::when_all`. All upstream HTTP connections (Qdrant, llama-server embed)
use persistent keep-alive clients - no TCP handshake per request.

**embed_ms 47x faster**: Python baseline used `sentence_transformers` (model
loaded in-process, PyTorch overhead, GIL contention). C++ calls the llama-server
`/v1/embeddings` endpoint over localhost HTTP with a persistent connection - the
model is already warm and no Python runtime is involved.

**anchor_ms 5.1x faster**: C++ coroutine suspension has near-zero overhead vs
Python asyncio task scheduling. Qdrant calls go over the same persistent client.
No Python object allocation on the hot path.

**guidance_ms 5.4x faster**: Same reasons as anchor. Single Qdrant search, no
Python overhead.

**Retrieval total saving**: (~273 - 6) + (~120 - 24) + (~57 - 11) = ~409ms
saved per request in the retrieval phase.

---

## LLM Phase - Root Cause Found and Fixed

### v1 regression: ENABLE_THINKING bug

C++ v1 showed ttft_ms=12,717ms (8.9x slower than Python). The root cause was
`tools/run_nz_tenancy.sh` explicitly setting `ENABLE_THINKING="true"`, overriding
the `config.hpp` default of `false`. The dedicated rewrite `Generator` was always
hardcoded to `enable_thinking=false` (line 1403 of main.cpp), so rewrite was fast
(~594ms) but main generation was slow (thousands of milliseconds TTFT) because Qwen3
generated a full `<think>...</think>` block (500-3000+ tokens) before the answer.

Fix: changed `ENABLE_THINKING="true"` to `ENABLE_THINKING="false"` in the run
script. The config default in `config.hpp` was already `false` (PR #55), but the
run script was bypassing it.

### v5 results with thinking=off

ttft_ms dropped from 12,717ms to **714ms mean** - now 2x faster than Python.
Total end-to-end time (10,709ms) beats Python (12,727ms) by 1.2x.

The remaining LLM time (generation_ms ~9.9s) is the model generating the full
answer (typically 200-400 tokens). This is bounded by the LLM throughput and is
the same model for both runtimes.

---

## p50 and p95 (C++ v5)

| Slot          | Python p50 | C++ v5 p50 | Python p95 | C++ v5 p95 |
|---------------|------------|------------|------------|------------|
| embed_ms      | 205ms      | 6ms        | 531ms      | 7ms        |
| anchor_ms     | 116ms      | 25ms       | 154ms      | 27ms       |
| guidance_ms   | 58ms       | 10ms       | 65ms       | 12ms       |
| ttft_ms       | 1,427ms    | 689ms      | 1,533ms    | 866ms      |
| generation_ms | 11,495ms   | 10,340ms   | 12,283ms   | 10,689ms   |
| total_ms      | 12,422ms   | 11,164ms   | 14,453ms   | 11,501ms   |

C++ retrieval variance is lower than Python (embed p95/p50 ratio: Python 2.6x,
C++ 1.2x). Python embed time varies widely because sentence_transformers can
stall on first-call model initialization. C++ has no such cold-start effect.

---

## Conclusion

C++ is faster than Python end-to-end once thinking is off. Retrieval is 5-47x
faster. TTFT is 2x faster. Total wall-clock time is 1.2x faster (10.7s vs 12.7s
mean). The remaining bottleneck is LLM token generation speed, which is identical
for both runtimes (same model, same server).

**Remaining open items**: none. Both items below were resolved.

- `context_chars` and `context_chunks` now appear in the timing SSE event.
- Drogon SSE crash fixed in PR #60: `pin_to_loop()` ensures the coroutine is
  pinned back to L_client before `generate_stream()`, eliminating the
  `Channel::remove()` data race on client disconnect mid-stream.
