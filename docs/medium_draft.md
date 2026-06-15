# From Python to C++23: Cutting RAG Retrieval Latency from ~800ms to ~35ms

*A production story about porting a FastAPI-based legal RAG system to C++23 with Drogon, Qdrant, and llama.cpp - with real benchmark numbers at every step. Full retrieval path: embed + legislation anchor + guidance, measured end-to-end.*

---

## Motivation: Back to Roots

I have been writing C and C++ since before Python was widely used. My mental model of software has always been shaped by the idea that the closer you are to the hardware, the faster your code will be. Not as a dogma, but as a practical truth that holds almost every time you push a system hard enough. Python is a fantastic tool for iteration and experimentation - I used it to build the first version of this system and it served its purpose well. But at some point you look at your profiler and you see GIL contention, per-call dictionary lookups, reference counting overhead on every object, and a runtime that needs surgery after every OS upgrade. That is the moment the C++ itch comes back.

This project was partly an engineering decision and partly a homecoming. I knew from experience that the gains would be real. What surprised me was how large they turned out to be.

---

## The Problem

astraea is a legal RAG (Retrieval-Augmented Generation) system I built to answer tenancy law questions in real time over a streaming HTTP connection. The Python/FastAPI version worked, but running it on Gentoo Linux was a constant maintenance burden: every host Python upgrade broke the venv, PyTorch and Playwright raced each other for GPU memory, and "it works on my machine" was a daily occurrence.

Beyond stability, performance was leaving real gains on the table. The Python GIL meant async I/O was fast but CPU-bound work serialized. Startup was slow. Memory fragmentation grew over hours. And there was no way to pack two requests into the LLM simultaneously without a full orchestration layer.

My goal: a single self-contained binary per jurisdiction. No Python. No venv. No surprises after `emerge -uDN @world`.

---

## What I Built

astraea.cpp is a C++23 RAG server. The retrieval pipeline runs three Qdrant searches in parallel via `co_await drogon::when_all(...)`: main corpus search, legislation anchor retrieval, and manual guidance injection. All three fire simultaneously and results are merged before LLM generation starts.

| Concern | Choice | Why |
|---|---|---|
| HTTP server | Drogon | Coroutine-native SSE, filters, connection pooling |
| JSON | glaze | Header-only, typed high-performance JSON |
| Vector DB client | Custom REST (Qdrant) | Direct HTTP, no heavyweight SDK |
| LLM client | Custom SSE client + TCP pool | Keep-alive + deferred connection return |
| Cache / semaphore | hiredis | Async, integrates with Drogon event loop |
| Logging | spdlog | Async ring buffer, structured |
| Allocator | mimalloc | ~2x faster small alloc vs system malloc |

The philosophy carried through every library choice: direct, low-overhead, close to the wire. No abstraction layers that hide what is actually happening on the socket.

This article is about retrieval and serving overhead, not claiming the full answer is instant. LLM generation still takes seconds. The point of the C++ port was to remove avoidable latency before generation starts, make the service stable on Gentoo, and allow the LLM runtime to become the only major bottleneck.

---

## The Optimization Journey

I ran benchmarks at each step. Every number below is from a live service on my AMD Ryzen AI 9 HX 370 (24 cores) laptop running Gentoo, with Qdrant 1.17.1 holding two collections: `nztt_moj` (698K points) and `nz_legal` (3.3M points), vectors at 768 dimensions.

The metric I care most about is `retrieve_ms` - the wall time from request received to "ready to send to LLM." This covers embed, all three parallel Qdrant searches, and result merging. It is the latency I can control; LLM generation time is a hardware ceiling.

### Step 0: Baseline

The naive C++ port with default Qdrant settings and a new TCP connection per request:

```
retrieve_ms (warm average): ~350ms
```

---

### Step 1: HTTP Keep-Alive TCP Pool

Every Qdrant search was opening a new TCP connection, paying the three-way handshake cost on every request. I built `LlmTcpPool` - an event-loop-local pool of idle `TcpClient` instances keyed by `(loop, "host:port")`.

The pool fast-path in `LlmStreamSession::start()`:

```cpp
if (auto idle = _pool->try_acquire(loop, endpoint_key)) {
    _client = std::move(*idle);
    // rebind callbacks, skip connect
    on_connection(_client.get(), ...);
    co_return;
}
// miss: create new connection as before
```

Deferred disposal ensures a connection is only returned to the pool after the chunked HTTP response terminator is received - never mid-stream. The pattern mirrors what a seasoned C network programmer would do by hand: keep the socket open, reuse it, close it only when you have to.

The cold vs warm delta tells the story clearly: my first request after a service restart costs ~318ms; a warm request on the same connection costs ~18ms. That gap is entirely TCP handshake and connection setup overhead. The pool makes warm the common case.

---

### Step 2: int8 Scalar Quantization on Qdrant

Both collections stored full float32 vectors (768 dimensions, ~3KB per point, ~2GB and ~10GB respectively). I applied int8 scalar quantization via the Qdrant PATCH API:

```json
PATCH /collections/nztt_moj
{
  "quantization_config": {
    "scalar": {
      "type": "int8",
      "quantile": 0.99,
      "always_ram": true
    }
  }
}
```

`always_ram: true` pins the quantized index in RAM. My Ryzen AI 9 HX 370 supports AVX-512 VNNI, which accelerates int8 dot-product distance computation directly in silicon. This is exactly the kind of hardware-level gain that only materializes when you eliminate the runtime overhead sitting between your data and the CPU instruction set. Python was not slowing down Qdrant's inner distance calculation. Qdrant still gets the VNNI benefit. The overhead I removed was around the edges: request orchestration, JSON handling, connection lifecycle, event-loop behavior, and the Python service runtime.

The Python value is from the previous service's observed retrieval path and was less finely instrumented than the C++ measurements. The C++ stage-by-stage numbers below are directly measured.

**Benchmark after quantization (5 questions, warm cache):**

| Question | retrieve_ms | generate_ms | total_ms |
|---|---|---|---|
| What is the bond limit? | 120 | 7628 | 8348 |
| Can a landlord enter without notice? | 135 | 10498 | 11557 |
| What are the grounds for termination? | 213 | 8882 | 9762 |
| How much notice for rent increase? | 211 | 8438 | 9176 |
| What is a fixed term tenancy? | 117 | 8571 | 9210 |
| **Average** | **159** | **8803** | **9611** |

From ~350ms to ~159ms. **2.2x improvement.**

---

### Step 3: Quantization Rescore for Precision

int8 ANN is fast but slightly less precise than float32. Qdrant supports a rescore pass: fetch 2x more candidates from int8 ANN, then re-rank with original float32 vectors. I added this to the `SearchReq` struct:

```cpp
struct QuantizationSearchParams {
    bool  rescore      = true;
    float oversampling = 2.0f;
};
struct SearchParams { QuantizationSearchParams quantization; };

struct SearchReq {
    std::vector<float>        vector;
    int                       limit;
    float                     score_threshold;
    bool                      with_payload;
    std::optional<FilterJson> filter;
    SearchParams              params;  // always included
};
```

`oversampling=2.0` means: if `limit=5`, fetch top 10 from int8 ANN, rescore all 10 with float32, return best 5. Precision returns to float32 quality. The additional int8 distance computations are cheap on VNNI; the float32 rescore touches a small candidate set already hot in CPU cache. Closer to the hardware means the machine does the work it is actually designed to do.

---

### Step 4: Segment Count Tuning - Match Cores

My Ryzen AI 9 HX 370 has 24 logical cores. Qdrant parallelizes a single search across segments. With default auto-segmentation, both collections had 7-8 segments - leaving 16+ cores idle during every search.

```bash
PATCH /collections/nztt_moj
{"optimizers_config": {"default_segment_number": 24}}

PATCH /collections/nz_legal
{"optimizers_config": {"default_segment_number": 24}}
```

After background re-segmentation, a single search fans out across all 24 cores. This is the same principle as choosing the right number of threads in a parallel C algorithm: more cores doing useful work simultaneously, less wall-clock time.

**Benchmark after 24-segment tuning (8 questions, sequential):**

| Question | retrieve_ms | Notes |
|---|---|---|
| What is the bond limit? | 318 | cold - TCP pool empty |
| Can a landlord enter without notice? | 135 | warming |
| What are the grounds for termination? | 126 | warming |
| How much notice for rent increase? | 82 | warming |
| What is a fixed term tenancy? | 53 | stable |
| Can a landlord inspect the property? | 58 | stable |
| What happens if landlord does not return the bond? | 52 | stable |
| How long can a tenant be in arrears before eviction? | 51 | stable |

Warm steady-state: **51-58ms**. A single isolated request after service restart: **18ms**.

---

### Retrieval Latency Across All Stages

# chart_cpp.png
The C++ port moved retrieval from hundreds of milliseconds to around 50 ms before LLM generation begins.

---

### Step 5: KV Cache Reuse via `cache_prompt`

llama.cpp supports `cache_prompt: true` in the completion request body. When requests share the same system prompt prefix, the KV cache for those tokens is retained in the active slot. Subsequent requests skip prefill for the shared prefix entirely.

My system prompt is 1,400-1,800 tokens (jurisdiction instructions + legislation anchor). With `cache_prompt: true`, warm requests pay prefill only for the user-specific portion: retrieved chunks plus the question, typically 300-600 tokens.

```cpp
struct GenerateReq {
    std::string           model;
    std::vector<Message>  messages;
    int                   max_tokens   = 2500;
    float                 temperature  = 0.2f;
    bool                  stream       = true;
    bool                  cache_prompt = true;  // added
};
```

Expected TTFT reduction on warm requests: **30-50%** for the prefill portion. Free gains - no architectural change, just telling the runtime to keep the work it already did.

---

### Step 6: Parallel LLM Slots

With retrieval now effectively free, the only bottleneck was the single llama.cpp slot. User B had to wait for user A to finish generation.

llama.cpp supports `--parallel N`, running N decode sequences simultaneously using batch processing. I changed one line in the service file:

```
# Before
--ctx-size 6144 --parallel 1

# After
--ctx-size 12288 --parallel 2
```

The ctx-size doubles because llama.cpp splits the total KV cache pool across slots (6144 tokens per slot, unchanged). I matched this with `LLM_GLOBAL_CONCURRENCY=2` in the astraea service. The `LlmTcpPool` already handled multiple concurrent connections - this was just opening the gate.

**Concurrent benchmark: 2 requests fired simultaneously**

| | Request 1 (bond limit) | Request 2 (landlord entry) |
|---|---|---|
| retrieve_ms | 35ms | 53ms |
| generate_ms | 11178ms | 9992ms |
| total_ms | 13374ms | 11035ms |
| wall-clock finish | 13380ms | 13381ms |

Both users got their answer within 1ms of each other at the 13.4s mark.

**Serial equivalent with `--parallel 1`:**
- User A finishes at ~10s
- User B starts generation, finishes at ~20s

With `--parallel 2`, user B waits **13.4s instead of ~20s** - a 33% reduction in worst-case wait time, with zero quality loss.



*[Replace with Gantt-style timeline chart]*

---

### Step 7: Protocol-level Retrieval Optimizations

After adding per-slot timing instrumentation (a `timing` SSE event at the end of each response carrying named slots for `embed_ms`, `anchor_ms`, `guidance_ms`, `ttft_ms`, etc.), I could measure each stage of the pipeline individually. The numbers on the pre-LLM path were worse than expected:

- `embed_ms` mean: **273ms** - the embed server was handling two concurrent requests per ask. One from the corpus retrieve, one from the legislation anchor retrieve. Both fired at the same time, but llama-server handles one embed at a time, so the second queued behind the first.
- `anchor_ms` mean: **120ms** - 2 sequential Qdrant searches (one per registered Act: RTA + HHS2019), then more sequential searches for synthetic-query section injection.
- `guidance_ms` mean: **57ms** - one Qdrant search returning all stored payload fields, most of which the code never reads.

Total pre-LLM retrieval overhead: **~450ms**. Three protocol-level fixes closed it.

**Shared embed vector.** `pipeline.retrieve()` and `retrieve_anchor()` both called `embed(retrieval_q)` independently. Same text, same model, embedded twice. The fix is straightforward: embed once before the parallel fanout, pass the resulting `vector<float>` to both branches.

```cpp
// Before: two independent embed calls fired concurrently, second one queues
auto [retrieved, anchor] = co_await when_all_pair(
    pipeline.retrieve(retrieval_q, ...),         // calls embed() internally
    retrieve_anchor(retrieval_q, ..., pipeline)  // calls embed() internally
);

// After: one embed, both branches reuse the vector
auto query_vec = co_await pipeline.embed(retrieval_q);
auto [retrieved, anchor] = co_await when_all_pair(
    pipeline.retrieve_with_vec(query_vec, ...),
    retrieve_anchor(retrieval_q, ..., pipeline, ..., &query_vec)
);
```

**Qdrant batch search.** The legislation anchor fired one HTTP call per registered Act, then more per synthetic-query injection - all sequential. Qdrant supports `/collections/{col}/points/search/batch`, which bundles N searches into one round-trip. `VectorStore::batch_search()` wraps this and the anchor loop collapses from N sequential awaits to one.

```cpp
// Before: sequential per-Act searches
for (const auto& src : leg_srcs) {
    auto batch = co_await leg_store->search(query_vector, tk, 0.0f, filt);
    raw.insert(raw.end(), ...);
}

// After: one HTTP call for all Acts
auto batches = co_await leg_store->batch_search(reqs);
for (auto& b : batches)
    raw.insert(raw.end(), ...);
```

**Payload field projection.** `with_payload: true` returned every stored field in the Qdrant response. Most fields are never read. Changed to `with_payload: {"include": ["text","title","case_id","url","date"]}`. Qdrant serializes less JSON; the parser processes less; the response arrives faster.

**Results (n=10 benchmark, warm service):**

| Slot | Before | After | Improvement |
|---|---|---|---|
| embed_ms mean | 273ms | 6ms | 45x |
| anchor_ms mean | 120ms | 25ms | 4.8x |
| guidance_ms mean | 57ms | 4ms | 13x |
| ttft_ms mean | 1429ms | 645ms | 2.2x |

Total retrieval overhead (embed + anchor + guidance): ~450ms to **~35ms**. The pre-LLM path is now invisible next to generation time (9-15s).

---

## Full Benchmark Summary

# retrieval_latency.png

# cocurrent.png

---

## What Made the Difference

In order of impact:

1. **int8 scalar quantization** - the enabler that made everything else possible. It cut my index memory footprint by 4x, meaning more of the hot data fits in L3 cache, and activated AVX-512 VNNI for hardware-accelerated distance computation. Everything downstream became faster because the data was already in a better place.

2. **24-segment tuning** - the biggest multiplier after quantization. One API call changed 7 segments to 24, matching my CPU core count. retrieve_ms dropped from 159ms to 55ms. Hardware parallelism was sitting unused.

3. **Rescore with oversampling** - recovered float32 precision at almost no additional cost. The extra distance computations are int8 (fast on VNNI), and the float32 rescore touches a small set already in cache.

4. **TCP connection pooling** - eliminated per-request TCP handshake overhead. The delta between cold (318ms) and warm (18ms) shows exactly what the handshake was costing me per request cycle.

5. **Parallel LLM slots** - doubled concurrent capacity with a config change and a Redis limit bump. The architecture was already ready; the single slot was an artificial ceiling.

6. **KV cache reuse** (`cache_prompt`) - free prefill savings on the shared system prompt. No complexity added, just preserving work already done.

7. **Shared embed vector** - embed the retrieval question once and pass `vector<float>` to both the corpus retrieve and the legislation anchor. Eliminated a hidden queued embed call per request. 273ms -> 6ms (45x).

8. **Qdrant batch search** - replaced sequential per-Act and per-synth-query Qdrant searches with `/points/search/batch`. One HTTP round-trip instead of N sequential awaits. anchor_ms: 120ms -> 25ms (4.8x).

9. **Payload field projection** - `with_payload: {"include": [...]}` instead of `true`. Qdrant sends only the five fields the code actually reads. guidance_ms: 57ms -> 4ms (13x).

---

## Why C++ and Not Go, Rust, or Java?

This question always comes up. A few honest answers:

Go, Rust, and Java can all be excellent production choices. For this project, C++ matched the way I wanted to build the service: coroutine-native HTTP, explicit connection pooling, typed low-overhead JSON, predictable allocation, and direct control over the hot path.

There is also the familiarity factor. When you have written C and C++ for years, you know exactly what a `shared_ptr` costs, when `std::move` removes a copy, when a buffer can be reused, and where a hidden allocation might appear. That knowledge is leverage.

Python helped me find the shape of the system. C++ helped me make that shape fast and stable.

---

## What Is Next

Retrieval is no longer the bottleneck (~35ms warm, full path). Generation at 9-15s is the remaining frontier:

- **NPU acceleration** - my laptop has an AMD XDNA 2 NPU (Strix Halo) that validated at 51 TOPS running a GEMM kernel. I am currently building iree-amd-aie to compile the embed model to a XCLBIN and offload it to the NPU entirely, freeing the GPU for LLM generation exclusively.
- **Speculative decoding** - a small draft model proposes tokens, the main model verifies in batch. Typically 2-3x generation speedup on predictable outputs. llama.cpp supports this natively.
- **Redis rewrite cache** - if query rewriting uses a separate LLM call (the Python version paid 514-940ms per request for this), caching rewrites keyed by normalized question eliminates the cost on repeat queries, which are common in a single-domain system.
- **Larger quantized model** - Qwen3-14B at Q4_K_M fits in approximately the same memory as Qwen3-8B at Q5_K_M. Better answers at similar latency.

---

## Conclusion

The port took a few weeks of evenings. The result is a binary that starts in under a second, uses a fixed memory footprint, and survives host OS upgrades without touching a dependency.

The performance numbers confirmed what I have believed since writing my first `malloc` call: the closer you are to the hardware, the faster things get. Not because C++ is magic, but because every layer of abstraction has a cost, and when you remove enough of them the hardware gets to do what it was designed to do - compute, at full speed, with no interpreter overhead, no Python object model in the hot path, and no runtime surprise between the request and the machine.

---

*Stack: C++23, Drogon, glaze, hiredis, spdlog, mimalloc, Qdrant 1.17.1, llama.cpp, Qwen3-8B-Q5_K_M. Platform: AMD Ryzen AI 9 HX 370 (24 cores), Gentoo Linux.*
