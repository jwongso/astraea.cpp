# Astraea C++ vs Python - End-to-End Timing Benchmark

Baseline collected 2026-06-14. Both runs hit the same service port (8001) on the
same host (Gentoo Linux, Mac Mini M4 Pro 48GB). Same LLM backend (Qwen3-8B-Q4
on port 8080, thinking enabled). Same Qdrant instance (port 6333). n=5 questions
each. All requests include `X-No-Log: 1`.

Python baseline file: `.training/benchmark_baseline.json`
C++ results file: `/tmp/cpp_benchmark.json`
Benchmark script: `.training/benchmark_timing.py`

---

## Results (mean, n=5)

| Slot              | Python (ms) | C++ (ms) | Ratio        |
|-------------------|-------------|----------|--------------|
| sanitize_ms       | ~0.0 (*)    | 0.1      | --           |
| route_ms          | 0.5         | n/a (+)  | --           |
| embed_ms          | 273.2       | 12.2     | **22.8x faster** |
| qdrant_ms         | 52.6        | n/a (+)  | --           |
| anchor_ms         | 119.7       | 35.2     | **3.4x faster**  |
| guidance_ms       | 57.4        | 10.7     | **5.4x faster**  |
| context_assembly  | 1.9         | n/a (+)  | --           |
| llm_wait_ms       | ~0.001      | ~0.0     | same         |
| ttft_ms           | 1,429       | 12,717   | 8.9x SLOWER  |
| generation_ms     | 11,325      | 24,491   | 2.2x SLOWER  |
| total_ms          | 12,727      | 25,305   | 2.0x SLOWER  |

(*) Python `sanitize_ms` was not instrumented in the baseline run (always 0).
(+) These sub-slots are not emitted individually by the C++ timing event. They
are folded into the parent slot (embed_ms covers the full embed HTTP round-trip;
route decision and context assembly are sub-microsecond and not separately timed).

---

## Retrieval Phase - C++ Wins

The three parallel tasks (embed+retrieve, anchor, guidance) run as coroutines
via `drogon::when_all`. All upstream HTTP connections (Qdrant, llama-server embed)
use persistent keep-alive clients - no TCP handshake per request.

**embed_ms 22.8x faster**: Python baseline used `sentence_transformers` (model
loaded in-process, PyTorch overhead, GIL contention). C++ calls the llama-server
`/v1/embeddings` endpoint over localhost HTTP with a persistent connection - the
model is already warm and no Python runtime is involved.

**anchor_ms 3.4x faster**: C++ coroutine suspension has near-zero overhead vs
Python asyncio task scheduling. Qdrant calls go over the same persistent client.
No Python object allocation on the hot path.

**guidance_ms 5.4x faster**: Same reasons as anchor. Single Qdrant search, no
Python overhead.

**Retrieval total saving**: (~273 - 12) + (~120 - 35) + (~57 - 11) = ~392ms
saved per request in the retrieval phase.

---

## LLM Phase - C++ Slower (Context Size Hypothesis)

ttft_ms is 8.9x higher in C++ (12,717ms vs 1,429ms). generation_ms is 2.2x
higher. Both runtimes call the same Qwen3 LLM with thinking enabled. The LLM
itself cannot be faster for Python by a factor of 9.

The most likely cause is **assembled context size**. Qwen3 thinking mode scales
thinking depth with input complexity - longer context (more legislation sections,
more case chunks) causes the model to generate a longer `<think>...</think>` block
before the answer. The C++ pipeline injects more content: it correctly retrieves
legislation sections and guidance (which Python may have partially skipped or
truncated in the baseline run), resulting in a larger prompt.

Supporting evidence:
- The Python baseline `rewrite_ms` slot (587ms-1082ms per request) reflects a
  question-rewrite LLM call that runs before retrieval. C++ does not have this
  slot in the timing event, suggesting it may handle rewrite differently or the
  time is absorbed elsewhere. If Python used the rewritten question for retrieval
  and C++ uses the original + anchor routes, context composition differs.
- C++ anchor_ms of 35ms (vs Python 120ms) with full route injection suggests C++
  retrieves more law sections per request, adding to prompt size.

**To verify**: log `context_texts.size()` and total context char count in both
and compare on the same question set.

---

## p50 and p95

| Slot          | Python p50 | C++ p50  | Python p95 | C++ p95  |
|---------------|------------|----------|------------|----------|
| embed_ms      | 205ms      | 13ms     | 531ms      | 15ms     |
| anchor_ms     | 116ms      | 32ms     | 154ms      | 47ms     |
| guidance_ms   | 58ms       | 11ms     | 65ms       | 11ms     |
| ttft_ms       | 1,427ms    | 13,560ms | 1,533ms    | 16,437ms |
| generation_ms | 11,495ms   | 26,058ms | 12,283ms   | 27,840ms |
| total_ms      | 12,422ms   | 27,201ms | 14,453ms   | 28,664ms |

C++ retrieval variance is lower than Python (embed p95/p50 ratio: Python 2.6x,
C++ 1.2x). Python embed time varies widely because sentence_transformers can
stall on first-call model initialization. C++ has no such cold-start effect.

---

## Conclusion

C++ retrieval is significantly faster and more consistent. The LLM phase results
are not directly comparable because the same LLM generates variable-length
thinking chains depending on context size and question complexity. The baseline
runs were taken at different times with potentially different context compositions.

A fair LLM comparison requires sending identical prompts (same system prompt, same
context text, same question) from both runtimes and measuring ttft/generation_ms.
That test is pending.

**Action items**:
1. Log assembled context size (char count + chunk count) in C++ timing event.
2. Run a context-controlled comparison: fix the context string, call the LLM
   directly from both Python and C++ with identical payloads, compare ttft.
3. Consider adding `rewrite_ms` to the C++ timing event if question rewrite is
   implemented, to make the comparison slot-for-slot compatible.
