# PLAN feedback — C++23 / Drogon / Glaze

Verdict on the three proposed picks, then the stack-level gaps that
need a decision before more code lands.

> **Read §9 (Target hardware) first.** Two very different ISAs in a
> 2-month window (x86-64 + RTX through July, ARM64 Apple Silicon from
> August) reshape several of the perf/SIMD/CUDA verdicts below.

---

## 0. Context — this is a learning project

Primary goal: **learn modern C++23 systems programming on a non-trivial,
real-world codebase** (HTTP/SSE server, async coroutines, SIMD, GPU,
modern build/test/profile tooling). Perf wins over the Python original
are a measurement and a motivator, not the success criterion. The
existing Python service keeps serving users; the C++ port is a vehicle.

This reframing changes several judgements in this document:

* The "minimum win" verdict on a C++ framework port is acknowledged
  and accepted. Sections §3 and §7 of this doc still describe where
  the realistic perf gains live (so you can measure them honestly),
  but they no longer carry a "don't bother" subtext.
* Several items I marked "skip permanently" or "never" in §7.7 are
  legitimate learning targets even if their production ROI is
  marginal or negative. They get reclassified below as "fine for
  learning, with eyes open about the production trade-offs."
* "Premature optimisation" warnings throughout the doc are relaxed.
  Adopting SIMD before profiling proves you need it is a perfectly
  good learning exercise; the only thing to keep is the *measurement
  discipline* (§7.6), because the learning happens in the diff
  between baseline and optimised numbers.
* The differential test harness (`finding.md` §2) becomes even more
  valuable in this framing: it lets you experiment freely with
  aggressive C++ techniques (custom allocators, SIMD passes, CUDA
  in-proc, lock-free containers) without ever losing parity with the
  Python reference. Land it early.

The technical recommendations below are unchanged. Only the
"adopt vs. skip" verdicts in §7.7 shift; an updated table is at the
end of §7.

---

## 1. C++23 — agree, with toolchain pin

The right baseline. Specifically these C++23 features will pay for
themselves repeatedly in this codebase:

* `std::expected<T, E>` — replaces the half-and-half exception/error
  pattern in `Config::from_env` and `SanitizeError`. Cleaner control
  flow on the request hot path.
* `std::print` / `std::println` — drop-in for `fprintf` without the
  format-string mismatch class of bugs. Useful for the structured
  logging that core needs.
* `<stacktrace>` — free crash diagnostics, attach to `SanitizeError`
  and any unhandled exception in the request handler.
* Deducing `this` — collapses the CRTP and overload boilerplate in
  `JurisdictionBase` derivatives.
* `std::flat_map` / `std::flat_set` — better cache locality than
  `std::map` for the small lookup tables in `ConfidenceConfig`,
  `low_priority_sections`, jurisdiction-side route indexes.
* `static operator()` — zero-overhead function objects for the
  per-route matchers and any predicate passed to standard algorithms.

### Toolchain pin

**Clang 18+ with libc++** (or libstdc++ if you prefer). Reasoning:

* Best C++23 library coverage today, including `std::print`,
  `std::expected`, `<stacktrace>` (libc++ 18+), `std::flat_map`.
* Cleanest coroutine codegen and best diagnostics.
* `clang-tidy` and `clangd` are already aligned with the same
  frontend — no version drift.

GCC 14+ is a viable secondary target; pin one as primary and treat
the other as a CI portability check, not a dual-supported config.

### Build configuration

* CMake ≥ 3.28 (needs `import std` if you ever want it) + Ninja.
* Two presets only, no more: `dev` (`-O0 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror`) and `prod`
  (`-O3 -flto=thin -march=x86-64-v3 -DNDEBUG`). `x86-64-v3` covers
  AVX2 and is the lowest sane bar for a 2026 server target;
  `march=native` only for local benchmarking.
* `-fno-exceptions` is **not** an option here — Drogon, glaze, and the
  stdlib all throw. Don't fight it. Use `std::expected` for *your*
  error paths and let exceptions remain for genuinely exceptional
  cases (bad-alloc, regex compile failure).

---

## 2. Drogon — agree, with three config decisions

Right choice for "HTTP server + SSE + HTTP client + WebSocket if ever
needed" in one package. It also brings its own event loop, which
matters for the async runtime question below (§4.1).

### Pin and trim

* **Drogon ≥ 1.9.6** (last release with C++20 coroutine support
  cleaned up and SSE-friendly `HttpResponse::newAsyncStreamResponse`).
  Pin in CMake/CPM, don't follow `master`.
* In the Drogon CMake config, **disable the modules you don't need**
  to cut binary size and compile time:
  - `BUILD_ORM=OFF` (no DB ORM — Astraea uses Qdrant + Redis).
  - `BUILD_BROTLI=OFF` unless you actually want brotli over SSE
    (you don't; SSE is plaintext deltas).
  - `BUILD_POSTGRESQL=OFF`, `BUILD_MYSQL=OFF`, `BUILD_SQLITE=OFF`.
  - `USE_SUBMODULE=OFF`; pull jsoncpp/trantor/etc via your package
    manager so versions are explicit.

### Use coroutines, not callbacks

Drogon supports both styles. Pick coroutines (`Task<HttpResponsePtr>`
handlers) and **only** that style for new code. Mixing callback and
coroutine code is the single biggest contributor to maintenance pain
in real Drogon services I've seen. The streaming SSE handler will
co_await chunks from the LLM client, write them into the response
stream, and `co_yield` back to the event loop between chunks — clean
and back-pressure aware.

### SSE specifics — verify these before committing

Drogon's `newAsyncStreamResponse` works, but two things bite people:

1. **Client disconnect detection** is via the callback returning
   `false` / the stream's `close()` — write a small spike that kills
   the curl connection mid-stream and verifies the LLM upstream
   `Task` actually cancels. This is the single most important
   behaviour to validate before you build retriever/generator on top.
2. **Per-connection back-pressure** — Drogon will buffer if the
   downstream socket can't keep up. For LLM streaming this is fine
   (LLM tokens are slow vs. network), but make sure the SSE handler
   periodically yields rather than tight-looping on incoming tokens.

### Alternative considered: Boost.Beast

Lower-level (you write your own router, middleware, connection
manager) but no opinions imposed. For an in-house framework like
Astraea, Drogon's "controllers + filters + lifecycle" is a small
acceptable abstraction tax in exchange for SSE, HTTP/2, WebSocket,
and an HTTP client batteries-included. Beast would mean +1–2 weeks
on the spine before any business logic. **Drogon is the right call.**

---

## 3. Glaze — agree, with one carve-out

Right choice for **internal and HTTP-facing** JSON. Specifically:

* `/ask/stream` request body parsing (`AskRequest` struct).
* SSE event framing (writing `data: { ... }\n\n` chunks).
* OpenAI-compatible client requests/responses to the LLM and
  embedder upstreams.
* Feedback writer payloads.
* Config file parsing if you ever move env-based config to TOML/JSON.

Why over the alternatives:

* **vs nlohmann/json:** Glaze is 5–20× faster on parse and write into
  typed structs, and the API is simpler once you've written one
  `glz::meta<T>` specialisation per struct (or used the reflection
  path). nlohmann's only real advantage is ubiquity, which doesn't
  matter for an internal service.
* **vs simdjson:** simdjson is faster at *raw DOM parsing*, but
  Astraea always parses into known struct shapes — Glaze does that in
  one pass without an intermediate DOM. For the write path simdjson
  is irrelevant (it's parse-only). Where Glaze loses to simdjson —
  arbitrary unknown-shape JSON — Astraea doesn't have that workload.
* **vs RapidJSON:** API is dated, no first-class reflection, similar
  speed to Glaze on most workloads.

### The carve-out — Qdrant should be gRPC, not JSON

Qdrant's REST API returns JSON, and Glaze would parse it well. But
**use Qdrant's gRPC API instead** for the retriever:

* Lower wire size (binary protobuf vs JSON for big vectors and
  payloads — meaningful when each chunk's payload is hundreds of
  bytes and you fetch dozens per request).
* Lower latency at the tail (single round-trip with streaming
  responses, no JSON serialisation overhead on either end).
* Strict schema — payload shape drift is caught at compile time, not
  at JSON parse time.

This means the retriever module pulls in `grpcpp` and the Qdrant
proto stubs (generated once with `protoc`). Glaze is **not** used on
the Qdrant path; it's used everywhere else. Document this split
clearly in the retriever module so nobody is tempted to "unify on
JSON for simplicity."

### Glaze pitfalls to know up front

* **Reflection requires either** `glz::meta<T>` specialisation **or**
  `BOOST_DESCRIBE_STRUCT`-style annotation **or** the pure aggregate
  path (no constructor, public members only). For Astraea's request
  structs the aggregate path is fine; for `RouteDecision` (which has
  semantic invariants worth keeping) write the `glz::meta`
  explicitly.
* **Compile-time cost** is real — Glaze does a lot at constexpr.
  Centralise the `glz::meta` specialisations in one or two TU's so
  the heavy instantiation isn't re-paid in every translation unit
  that just *uses* the structs.
* **Error messages** when a field name doesn't match are still
  cryptic. Pay the up-front cost of writing one tiny unit test per
  JSON schema (round-trip a sample, assert equality). Catches >90% of
  schema drift in CI.

---

## 4. Gaps in the current plan — decide now, not later

The three picks above settle ~30% of the framework stack. These are
the remaining choices that need to be locked in before more modules
land, because each one ripples through every subsequent file.

### 4.1. Async runtime / coroutine executor

Drogon brings its own event loop (trantor). That handles the HTTP
server side. But the **outbound** clients (Redis, future Qdrant gRPC
via async stub, any in-process work that wants to be coroutine-y)
need to live in some executor. Options:

* **Use Drogon's loop for everything.** Simplest. Wrap Redis and
  gRPC calls as `Task<T>` that schedule on the same event loop. One
  runtime, no synchronisation between loops.
* **Drogon for HTTP, separate Asio runtime for everything else.**
  More flexible, but you'll fight thread-affinity issues and end up
  serialising data across loop boundaries.
* **stdexec (P2300) on top of Drogon.** Future-proof but the
  ecosystem isn't there yet; Drogon doesn't natively integrate.

**Recommendation: one loop, Drogon's.** Add a thin
`co_await drogon::async_run([&] { /* blocking work */ })` for the
genuinely blocking calls that can't be made async (currently: none
that I see). Revisit only if profiling shows the single-loop model
saturating.

### 4.2. Redis client

Used by `core/queue.py`, `core/session.py`, `core/feedback.py`,
`core/embedder.py` (cache).

* **redis-plus-plus** — most mature C++ client, built on hiredis,
  has async API via libuv/libev. Doesn't natively know Drogon's
  loop; wrap it.
* **boost::redis** — header-only, Asio-based, coroutine-first.
  Doesn't run on Drogon's loop either.
* **Raw hiredis with async API** — full control, more code.

**Recommendation: redis-plus-plus** in sync mode initially (Redis
calls are <1ms locally; not worth the loop integration pain). Move
to async only if profiling shows blocking time matters. The session
and feedback writers can use a separate worker thread pool —
`std::jthread` + a bounded MPMC queue (concurrentqueue from
moodycamel, or your own).

### 4.3. LLM / embedder upstream client

OpenAI-compatible HTTP, streaming SSE response from the LLM.

* **Drogon's HttpClient** — handles both, lives on the same loop,
  supports streaming response callbacks. Use this. Don't pull in
  curl.

For the LLM streaming response, parse each SSE `data: { ... }` line
incrementally with Glaze (`glz::read_json<ChatCompletionChunk>` on
each line). Forward the delta straight to the client SSE stream.

### 4.4. Vector DB client

As above: gRPC + Qdrant proto stubs. **Pin Qdrant proto to a
specific Qdrant version** (the proto changes between minor
releases). Vendor the generated stubs into the repo so builds aren't
hostage to `protoc` versioning.

### 4.5. Reranker

Two paths:

* **In-process ONNX Runtime + bge-reranker model.** Fastest, no
  network hop. Requires shipping the model and ONNX runtime library
  in the container.
* **HTTP call to a reranker service** (same shape as embedder).
  Slower but operationally simpler.

The Python code (`core/reranker.py`) is currently the second
pattern. Stay consistent unless there's a strong reason to in-process
it. ONNX in-process is a follow-up optimisation, not v1.

### 4.6. Logging

**spdlog**. No serious alternatives. Async logger on its own thread,
JSON formatter for structured fields, level-controlled at runtime
from `Config`. Avoid `std::print` for production logs (no level
control, no async, no rotation).

### 4.7. Testing

* **doctest** for unit tests — fastest C++ test framework to compile,
  matters because Glaze + Drogon already inflate build times.
* **nanobench** for micro-benchmarks (header-only, single file,
  trivially gives you ns/op and instructions-per-cycle on Linux
  with perf counters enabled).
* **The differential harness** from `finding.md` §2 — pybind11 +
  pytest against the Python fixtures. This is the single highest-
  value test asset for the whole project.

### 4.8. Dependency management

**CPM.cmake** — simplest, declarative, no external tool. Vcpkg and
Conan are heavier for what is effectively six dependencies (Drogon,
Glaze, spdlog, doctest, nanobench, redis-plus-plus, grpcpp+protobuf).
Pin every dependency to a specific tag in the top-level CMake — no
floating refs.

### 4.9. Unicode for §1.1 strip table

Don't pull in ICU just for Cc/Cf stripping. Write a tiny build-time
codegen step:

```cmake
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/gen/unicode_strip.inc
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/gen_unicode_strip.py
            ${CMAKE_SOURCE_DIR}/third_party/UnicodeData.txt
            ${CMAKE_BINARY_DIR}/gen/unicode_strip.inc
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_unicode_strip.py
            ${CMAKE_SOURCE_DIR}/third_party/UnicodeData.txt
)
```

Emits a `constexpr std::array<std::pair<uint32_t, uint32_t>, N>` of
sorted ranges. `sanitize.cpp` does a binary search per non-ASCII
codepoint. ~150 entries, ~5 KB binary, zero runtime cost beyond log₂.

### 4.10. Aho-Corasick for routing

Header-only `aho-corasick-cpp` (~500 LOC, MIT) or hand-roll (~150
LOC, no dep). Build the automaton once per `Jurisdiction` at startup
from the union of all route terms; match in one pass per request.

---

## 5. What to *not* add (fight bloat creep early)

Spelling these out now saves arguments later:

* **No Boost** unless absolutely required. Drogon, Glaze, spdlog,
  doctest are all Boost-free. Pulling in Boost.{Asio,Beast,Hana,…}
  for one feature costs >30s of compile time per TU forever.
* **No ORM.** Astraea has no SQL. Don't link Drogon's ORM module.
* **No template engine.** Frontend is static HTML served separately.
* **No protobuf for anything that isn't gRPC.** Internal IPC, if it
  ever happens, should be Glaze BEVE (binary) or flatbuffers, not
  protobuf.
* **No Python-in-C++ embedding.** Don't `<Python.h>` your way out of
  porting a hard module. Either port it cleanly or shell out via
  gRPC/HTTP to a separate service.
* **No fmtlib separately.** C++23 `std::format` is sufficient and
  Drogon/spdlog bring fmt transitively anyway.

---

## 6. Locked stack summary

| Concern | Pick | Why |
|---|---|---|
| Language | **C++23** | settled |
| Compiler | **Clang 18+, libc++** | best C++23 lib coverage, sanitisers, clangd |
| Build | **CMake ≥ 3.28 + Ninja** | dev/prod presets, no more |
| Deps | **CPM.cmake** | declarative, pinned, no extra tool |
| HTTP server | **Drogon ≥ 1.9.6, coroutines, ORM off** | SSE + HTTP client + WebSocket, one loop |
| JSON | **Glaze** | fastest typed JSON, simpler than nlohmann |
| Qdrant | **grpc++ + Qdrant proto stubs** | binary, lower tail latency than REST |
| Redis | **redis-plus-plus**, sync to start | simplest, fast enough on localhost |
| LLM/embedder client | **Drogon HttpClient + Glaze per-chunk** | same loop, no curl |
| Logging | **spdlog**, async, JSON formatter | only serious option |
| Tests | **doctest** + **nanobench** + pybind11 differential | fast compile, fixtures from Python |
| Unicode strip table | **codegen from UnicodeData.txt** | no ICU dependency |
| Aho-Corasick | **header-only or hand-rolled** | one-time startup cost, zero per-request |

---

## 7. Hardware & SIMD acceleration — future bottleneck plan

Speed wins live where the time actually is. The framework hot path
(sanitize → route → retrieve → rerank → generate) has very different
optimisation surfaces at each stage. Most user wall-clock time is in
LLM generation, which is external; the framework itself has
microsecond budgets that don't justify SIMD/CUDA for its own sake.

Below is the realistic roadmap, ordered by leverage and gated by the
profiling signal that justifies adopting each one. **Nothing here
gets adopted before §8's milestones are running and producing
measured baselines.** Premature SIMD/CUDA optimisation has burnt more
C++ codebases than any other category of decision.

### 7.1. Free wins — take immediately

* **mimalloc** as the process allocator (CMake link or `LD_PRELOAD`).
  10–30% improvement on alloc-heavy paths (RouteDecision build,
  JSON, per-request string buffers). Zero code change. tcmalloc and
  jemalloc are fine alternatives; mimalloc wins most 2024–2026
  Linux-server benchmarks on this workload shape.
* **Drogon's existing parallelism** — one event loop per core, work
  stolen across them. Don't add OpenMP on top of it; they fight for
  CPU.
* **ONNX Runtime internal threading** (when in-process inference
  ever lands) — uses MLAS/oneDNN/OpenMP under the hood. Configure
  via `SessionOptions::SetIntraOpNumThreads`, never try to outsmart
  it from outside.

### 7.2. SIMD — three places it earns its keep

All three have well-measured 3–10× wins in similar workloads. None
is worth adopting before the scalar version is correct and profiled.

#### 7.2.1. Sanitize UTF-8 validation + Cf strip → **simdutf**

[simdutf](https://github.com/simdutf/simdutf) (Lemire et al., MIT).
UTF-8 validation at ~10 GB/s using AVX-512/AVX-2/NEON. Drop-in for
the §1.1 strip work:

```cpp
if (!simdutf::validate_utf8(text.data(), text.size()))
    throw SanitizeError("invalid UTF-8");
// then walk codepoints via simdutf::convert_utf8_to_utf32 and consult
// the generated Cf strip table (§4.9)
```

Brings sanitize on a 1 200-char request from ~5 µs (scalar with
table) down to <1 µs. **Trigger:** land alongside the §1.1 correctness
fix — the cost-of-adoption is one CMake line, so no point waiting.

#### 7.2.2. `normalize_query` byte classification → hand-SIMD or simdutf

The byte loop classifies each input byte into a small set of
classes ({ASCII letter, hyphen, whitespace, high-bit start}). One
AVX-2 register processes 32 bytes per iteration with a handful of
`_mm256_cmp*` instructions. ~5× over scalar.

For portability, use **[Google Highway](https://github.com/google/highway)**
(`hwy::`) rather than raw intrinsics — single-source AVX-512 / AVX-2
/ SSE4 / NEON / SVE codegen, same speed as hand-tuned per ISA. Future
ARM hosting (Graviton) costs nothing. `std::simd` in C++26 will
replace this; Highway is the bridge until then.

**Trigger:** when normalize_query exceeds 10% of pre-LLM request time
in profiling.

#### 7.2.3. Aho-Corasick state-machine scan → SIMD AC variants

Standard AC is pointer-chasing — not naturally SIMD-friendly. For
*small* automata (Astraea's ~1 370 terms fit in a compact double-
array trie), specialised SIMD implementations exist:
[MultiPatternMatchingSIMD](https://github.com/lemire/MultiPatternMatchingSIMD)
and the technique used inside Intel's HyperScan. 2–4× over scalar.

**Trigger:** route × terms > ~10 000 entries. Today's 1 370 puts
scalar AC well under 1 µs per request; SIMD AC is not worth the
maintenance cost yet.

### 7.3. CUDA — only at model boundaries, never in the framework core

CUDA matters in three places, all currently **external services** in
the Python stack. The right policy: keep them external in C++ too,
with one exception.

* **LLM generation.** Stays external. The biggest lever in the whole
  system is here: switching the upstream from llama.cpp to
  **vLLM** / **SGLang** / **TensorRT-LLM** with FP8 or AWQ
  quantisation typically halves token latency. Out of framework
  scope; track it as an ops decision, not a code decision.
* **Embedder.** Stays external. Hugging Face TEI on GPU, or a custom
  ONNX-Runtime-CUDA service. In-process embedding via ONNX-RT-CUDA
  ties the binary to ~1 GB of CUDA libraries and a specific driver
  version. Not worth the operational pain.
* **Cross-encoder reranker.** **The one in-process CUDA candidate**
  if reranker latency becomes the dominant non-LLM cost. ONNX
  Runtime with the CUDA execution provider gives you transformer
  inference in-proc via a clean C++ API and saves the 5–20 ms
  network hop. Decide later, based on tail-latency data.

**No custom CUDA kernels.** Every CUDA workload here (matmul,
softmax, attention, top-k) is already in **cuBLAS**, **cuDNN**,
**CUTLASS**, or **oneDNN**. Reach for a library kernel; never write
an intrinsic.

**No in-process GPU vector search** (cuVS, FAISS-GPU). Qdrant owns
index persistence, replication, filtering, HNSW maintenance — pulling
the index in-process to get GPU acceleration is a multi-month
re-architecture for marginal gain. Skip permanently.

### 7.4. OpenMP — wrong primitive for the request hot path

OpenMP does not fit an async server. Spawning an OpenMP team inside
a coroutine handler adds 10–100 µs of team-spawn overhead per
request, competes with the event loop for cores, and is undefined
behaviour if any `co_await` runs inside the parallel region.

OpenMP **does** fit three things, none on the hot path:

* **Offline ingestion** (`scripts/ingest_decisions.cpp`) — bulk
  embedding of thousands of documents. Trivial `#pragma omp parallel
  for`. Use freely.
* **One-shot startup work** — Aho-Corasick / route automaton
  construction, model loading. Cuts cold-start time on large
  jurisdictions.
* **Transparently inside ONNX Runtime / oneDNN / MLAS** — set
  `OMP_NUM_THREADS` to bound it; don't layer your own on top.

For everything else, prefer **Intel oneTBB**: composes with async
runtimes, has work-stealing, and concurrent containers that are
actually correct under contention.

### 7.5. Other libraries worth knowing about

* **liburing** — async I/O on Linux 5.1+. Drogon uses epoll via
  trantor; switching to io_uring saves syscall overhead at very high
  RPS. Only worth it if perf shows syscalls dominate; Astraea's
  per-request syscall count is low (LLM streaming dominates).
  Probably never.
* **moodycamel::ConcurrentQueue** — header-only MPMC queue for the
  feedback writer / session GC / any background worker. Faster than
  boost::lockfree on this workload shape.
* **wyhash / xxHash** — fast non-cryptographic hashing for the
  `unordered_set<string_view>` hot path if route-set probing becomes
  hot. `std::hash` is fine until proven otherwise.
* **EVE** / **xsimd** — alternative SIMD wrappers to Highway. EVE has
  the cleanest C++20 syntax; Highway has the most serious production
  usage (JPEG XL, Chromium). Pick one.
* **DPDK / kernel-bypass networking** — wildly overkill for any
  human-facing service. Skip permanently.

### 7.6. Profiling-first discipline

Every item above is gated on **measured** evidence. Two tools to
install in the dev container from milestone 1:

* **perf + FlameGraph** — Linux perf for CPU sampling, Brendan
  Gregg's FlameGraph for visualisation. Zero application changes.
* **Tracy** — frame-by-frame profiler with sub-µs resolution and
  coroutine support. Add `ZoneScoped` to the request handler and
  each pipeline stage; live trace per request. Build cost is real
  (~30s on first include); guard behind a CMake option
  (`ASTRAEA_TRACY=ON`).

Establish the baseline on the first end-to-end working build
(post-milestone-3 in §8). Every SIMD / CUDA / OpenMP decision after
that is driven by what the profiler says, not by what reads well in
a plan doc.

### 7.7. Locked acceleration policy (revised for learning context)

The original "never" rows assumed pure production-ROI lens. With
learning as the primary goal, they reclassify as below. "Production
verdict" is what a hard-nosed ops review would say; "learning
verdict" is what makes sense for this project.

| Lever | Production verdict | Learning verdict | Notes |
|---|---|---|---|
| mimalloc | adopt now | adopt now | trivial, real win |
| simdutf for sanitize | adopt with §1.1 | adopt with §1.1 | first SIMD contact, clean win |
| Highway-based SIMD for normalize | profile-gated | **adopt early** | excellent SIMD-learning surface |
| SIMD Aho-Corasick | scale-gated | **adopt when AC lands** | textbook SIMD problem, well-documented |
| ONNX-RT CUDA reranker in-proc | profile-gated | **adopt early** | best CUDA-via-library learning path |
| OpenMP in ingest scripts | freely | freely | classic OpenMP use case |
| oneTBB for parallel-for | freely | freely | composes with async |
| Hand-written CUDA kernels | never | **fine to try** | educational; ship the library version |
| In-proc GPU vector search (cuVS/FAISS-GPU) | never | **fine to explore** | great GPU-data-structures learning |
| OpenMP on the request hot path | never | educational counter-example | implement, measure, then remove |
| DPDK / io_uring / kernel bypass | never | **fine to try** | systems-level networking depth |
| External LLM perf (vLLM / TRT-LLM) | biggest ops lever | track separately | not framework code |

The discipline that *does* survive the reframing: **measure before and
after every change**. The learning value of adopting SIMD/CUDA/OpenMP
collapses to zero if you can't quantify what each change cost or
gained. §7.6's profiling stack (perf + FlameGraph + Tracy) is the
non-negotiable enabler.

A reasonable learning arc through this stack, ordered to maximise
the diversity of techniques covered:

1. Scalar baseline (milestones 1–3 in §8). Establish ground-truth
   numbers with the differential harness keeping parity.
2. mimalloc + simdutf. First "free wins" pass.
3. Highway SIMD for `normalize_query`. First hand-SIMD pass.
4. Aho-Corasick scalar implementation, then SIMD variant. Compare.
5. ONNX-RT CPU reranker in-proc; then swap to CUDA EP. Compare.
6. Optional dives that are pure learning: hand-written CUDA kernel
   replicating the reranker's softmax, io_uring backend for trantor,
   cuVS prototype for one route's vector search. Throw the code
   away after measuring; the learning sticks.

---

## 8. First three milestones if this stack is approved

1. **Skeleton + sanitize parity.** CMake project, Drogon "hello SSE"
   handler streaming 10 synthetic chunks, sanitize.cpp §1.1 fix +
   pybind11 binding + first differential test against the Python
   `test_sanitize.py` corpus. End state: `cmake --build && ctest`
   runs Python parity tests green. ~2–3 days.
2. **Routing parity.** Fix routing.cpp §1.2–§1.4, port Aho-Corasick,
   wire the full route_fixtures harness. End state: every
   `RouteFixture` in nz_tenancy passes against the C++ implementation
   via the differential harness. ~3–4 days.
3. **Retriever spine.** grpc++ + Qdrant stubs, the retriever module
   issuing one async query and returning N chunks. No reranker, no
   anchor logic yet. End state: SSE handler can answer with raw
   retrieved snippets, no LLM yet. ~3–4 days.

After those three, the framework spine is real and every subsequent
module (anchor, reranker, generator, queue, session, feedback) is a
straight port against a known-correct foundation.

---

## 9. Target hardware — current and August

Two very different platforms in a 2-month window. This changes
portability from "nice to have" to "hard requirement from day one"
and gives CUDA a fixed expiry date.

### 9.1. Now through July — AMD Ryzen AI 9 HX 370 + RTX 4060 mobile (8 GB)

**CPU:** **Ryzen AI 9 HX 370** (Strix Point, mid-2024 / sold through
2025). Hybrid: **4× Zen 5 performance cores + 8× Zen 5c density
cores = 12 cores / 24 threads**. **Radeon 890M iGPU** (RDNA 3.5, 16
CUs, ~2.9 GHz) and — crucially — **XDNA 2 NPU at ~50 TOPS** back
in the picture.

Notable architectural points for the framework:

* **Hybrid scheduling matters.** Unlike Intel's P/E split, Zen 5
  and Zen 5c share the same ISA and AVX-512 capability at the same
  vector width — Zen 5c is density-optimised, not stripped down. So
  no SIMD-width gotchas. But Zen 5c runs at lower frequencies
  (~3.3 GHz boost vs. ~5.1 GHz on Zen 5), so the request hot path
  benefits from **CPU affinity pinning** to the Zen 5 cores while
  background work (feedback writer, session GC, log flushing,
  ingestion) gets pushed onto Zen 5c. Linux: `pthread_setaffinity_np`
  or `sched_setaffinity` against the mask of Zen 5 cores (read
  topology from `/sys/devices/system/cpu/cpu*/topology/`). Drogon's
  event-loop threads should sit on Zen 5; background `std::jthread`
  workers on Zen 5c.
* **Full 512-bit AVX-512 datapaths** on both core types, with the
  full feature set: VBMI2, VNNI, BF16, IFMA, BITALG, VPOPCNTDQ.
  `simdutf`, Highway, AC matching, small-vector reranker math —
  all benefit.
* **No 3D V-Cache** on this part (X3D is the desktop / Fire Range
  story). L3 is 24 MB shared — enough to fit the route automaton
  comfortably, but the cache-locality experiment I'd suggested with
  the 9955HX3D doesn't apply.

* **CMake/compiler target:** `-march=znver5` for max codegen, or
  `-march=x86-64-v4` for a portable AVX-512 baseline if you ever
  want to run on Intel Sapphire Rapids / Granite Rapids too. `prod`
  preset in §1 should be `-march=znver5` on this machine.
* **AVX-512 policy:** use it freely **through Highway**, never via
  intrinsics. The same Highway source recompiles to NEON on M4
  without code changes — that's the whole point.
* **XDNA 2 NPU** — the highest-novelty learning surface available
  through July. Access path:
  - AMD's Ryzen AI software stack: XDNA driver + ONNX Runtime
    **VitisAI EP**. Mature on Windows; Linux support has improved
    through 2025 but remains the rougher path.
  - Best fit: run the **reranker** on the NPU (small transformer,
    fixed shapes, well-suited to the NPU's int8/bf16 strengths).
    Compare CPU (Highway/AVX-512) vs. GPU (CUDA on RTX 4060) vs.
    NPU (VitisAI EP) latency and power on the same ONNX model.
    Three-way comparison is rare and educational.
  - Do not put NPU on the request hot path until you have ops
    confidence in driver stability. Treat it as a `--backend=npu`
    toggle behind the abstract `Reranker` interface.
* **Radeon 890M iGPU** — a tertiary accelerator. Tooling in 2026:
  ROCm/HIP support for RDNA 3.5 on Linux is still rough; Vulkan
  compute via llama.cpp's Vulkan backend is the mature path. With
  the RTX 4060 sitting in the same machine and CUDA tooling years
  ahead, **the iGPU is not worth a dedicated learning sprint** —
  but keep it in mind as the only on-die GPU you'll have in August
  if you ever benchmark Vulkan compute portability (the same
  llama.cpp Vulkan backend runs on M4's Metal-via-MoltenVK).

**GPU:** RTX 4060 mobile, Ada Lovelace, 8 GB GDDR6, 3 072 CUDA cores,
4th-gen Tensor cores with FP16/BF16/FP8 support.

* **8 GB VRAM is the binding constraint.** What fits:
  - bge-m3 embedder FP16 (~2 GB) — fits comfortably.
  - bge-reranker-v2-m3 FP16 (~600 MB) — fits trivially.
  - Small LLMs quantised: Qwen2.5 3B Q4 (~2 GB), Llama 3.2 3B Q4
    (~2 GB), Phi-3 mini Q4 (~2.4 GB). Useful for development, not
    for production-quality legal answers.
  - Mid-size 7B–8B Q4 (~5–6 GB) fits, but tight with KV cache on
    longer contexts. Workable for prototyping.
  - **Anything ≥13B will not fit usefully.** Continue calling
    a remote LLM (the current Python `LLM_BASE_URL` pattern) for
    real answer quality.
* **CUDA learning window:** through July only. Build the in-process
  ONNX-Runtime-CUDA reranker now, learn the EP architecture, measure
  the latency win versus an HTTP reranker call. In August this code
  becomes inactive (no CUDA on Apple Silicon) — that's expected; the
  *learning* persists.
* **TensorRT / TensorRT-LLM:** worth one experiment on this hardware
  for the reranker (FP8 inference, kernel autotuning). Higher
  learning value than raw CUDA kernels, since you learn the
  optimised-inference toolchain. Same expiry in August.

### 9.2. August — Apple M4 Pro (48 GB unified RAM)

Different ISA, different accelerator story, dramatically more RAM.

**CPU:** ARM64, ~10–14 performance + efficiency cores depending on
exact M4 Pro SKU.

* **NEON** (128-bit SIMD) — standard ARM vector ISA. Highway covers
  this transparently; the Highway-based SIMD you write on Ryzen
  recompiles unmodified.
* **SME (Scalable Matrix Extension)** — M4 is the first Apple chip to
  ship SME. C intrinsics live in `<arm_sme.h>`. Library support is
  thin in mid-2026 but improving rapidly. **The highest-novelty
  learning target available in August.** Worth a focused experiment
  re-implementing a small reranker matmul in SME, comparing against
  Accelerate's BNNS.
* **AMX (Apple Matrix coprocessor)** — undocumented, accessible only
  via `Accelerate.framework`. Use BNNS or vDSP and let Apple's
  library schedule onto AMX. Don't try to drive AMX directly.

**GPU / Neural accelerators:**

* **Metal Performance Shaders Graph (MPSGraph)** — Apple's optimised
  graph runtime, accessible via Objective-C++ (a `.mm` file from
  your C++ TU). The natural path for in-process inference.
* **CoreML** — model format + runtime. ONNX → CoreML converter
  exists. CoreML can dispatch to ANE (Apple Neural Engine) which
  the GPU cannot. Best end-to-end perf for fixed-shape inference.
* **MLX** — Apple's NumPy-like array library, C++ API exists but is
  less polished than Python. Unified-memory zero-copy between CPU
  and GPU is the killer feature.
* **llama.cpp Metal backend** — battle-tested, easy to embed. Good
  fallback for LLM serving.
* **No CUDA. No TensorRT. No NVIDIA tooling.** All §9.1 GPU work
  becomes inactive code paths. ONNX Runtime is the abstraction that
  saves you: same C++ API, swap `CUDAExecutionProvider` for
  `CoreMLExecutionProvider` at session creation.

**Unified memory at 48 GB changes framework topology.** What was
necessarily-external in Python becomes *plausibly in-process* in C++:

* **Embedder in-process** — bge-m3 INT8 (~1 GB) loaded into the
  framework process via ONNX Runtime + CoreML EP. Saves a network
  hop. No VRAM contention because unified memory.
* **Reranker in-process** — same pattern. ~5–20 ms saved per
  request.
* **Local LLM in-process or sidecar** — Qwen2.5 32B Q4 (~20 GB),
  Llama 3.1 70B Q4 (~40 GB) both fit. Via llama.cpp's Metal backend
  in-process, or as a localhost sidecar serving OpenAI-compatible
  HTTP. Sidecar is operationally cleaner.

The Python design's "every model is a remote service" pattern was a
forced choice on x86-with-discrete-GPU and 8 GB VRAM. On M4 Pro
48 GB it stops being forced; the C++ port can pick whichever
topology you want to learn from. **My suggestion:** keep the
external-service pattern as the default (matches the production
deployment story), but build an in-process embedder + reranker as a
compile-time alternative (`-DASTRAEA_INPROC_INFERENCE=ON`). Same
abstract `Embedder` / `Reranker` interface, different concrete
implementation. Excellent learning surface for both topologies.

### 9.3. Cross-platform implications

These tighten several earlier decisions:

* **Compiler: Clang, no second choice.** Linux/Ryzen → Clang 18+ via
  apt or Homebrew LLVM. macOS/M4 → Apple's bundled Clang in Xcode 16
  (C++23 coverage is good), or upstream LLVM via Homebrew if you
  want bleeding-edge `std::print` / `<stacktrace>` on libc++. GCC
  drops from the picture entirely — there's no `g++` story on macOS
  worth maintaining.
* **SIMD: Highway is mandatory, not optional.** `std::simd` (C++26)
  not ready on either toolchain in mid-2026; revisit in 2027. Raw
  `<immintrin.h>` or `<arm_neon.h>` intrinsics are banned from the
  framework code (fine for one-off learning experiments in a
  separate `experiments/` tree).
* **ML runtime: ONNX Runtime as the single abstraction.** Build with
  `CUDAExecutionProvider` + `CPUExecutionProvider` on Ryzen, switch
  to `CoreMLExecutionProvider` + `CPUExecutionProvider` on M4. Same
  model file (.onnx), same C++ code, EP selected at session
  creation. This is the single most important portability decision
  in the document.
* **Build system: CMake presets per platform.** `dev-linux-znver5`,
  `prod-linux-znver5`, `dev-mac-arm64`, `prod-mac-arm64`. CI runs
  all four (Linux runner + GitHub `macos-14` runner with M-series
  silicon).
* **CI: matrix from day one.** Don't wait until August to discover
  the codebase doesn't compile on ARM. Add macOS-ARM to CI as soon
  as the skeleton exists. Free on GitHub Actions.
* **Dependencies: re-check every one for ARM64 + macOS support.**
  Drogon: ✅ (works on macOS, ARM64). Glaze: ✅ (header-only,
  portable). spdlog: ✅. doctest: ✅. nanobench: ✅. mimalloc: ✅
  (first-class macOS + ARM support). simdutf: ✅ (NEON path). Highway:
  ✅. redis-plus-plus: ✅ via hiredis. grpc++: ✅ but slow to build
  on macOS — budget for it. ONNX Runtime: ✅ on both, separate
  prebuilt binaries per platform.
* **No CUDA in core framework code.** Any CUDA code lives in
  `core/reranker/cuda_backend.cpp` guarded by
  `#ifdef ASTRAEA_HAVE_CUDA` and a CMake `option(ASTRAEA_CUDA OFF)`.
  Same pattern for `coreml_backend.mm` (Objective-C++ glue, only
  compiled on Apple) and `metal_backend.mm`. The
  abstract `Reranker` interface in `core/reranker/reranker.hpp`
  stays platform-neutral.

### 9.4. Updated learning arc (timeline-aware)

Reordered from §7.7's arc to align with the hardware window:

**Through July (Strix Point + RTX 4060, learning-budget window):**

1. Milestones 1–3 from §8 (scalar baseline + harness).
2. mimalloc + simdutf (both portable, both immediate). Add **CPU
   affinity setup**: pin Drogon event-loop threads to the 4 Zen 5
   cores, background workers to the 8 Zen 5c cores. Measure
   request-latency tail with and without pinning; document the
   delta.
3. Highway SIMD for `normalize_query` — write once on Strix Point
   with AVX-512, verify it compiles for NEON in CI (cross-build,
   even before the M4 arrives).
4. Scalar Aho-Corasick → SIMD Aho-Corasick via Highway. Compare
   both on AVX-512.
5. **GPU learning sprint, part A:** ONNX-RT-CUDA reranker in-proc
   on the RTX 4060. Benchmark vs HTTP reranker. Then TensorRT for
   the same reranker; benchmark FP16 vs FP8.
6. **GPU learning sprint, part B:** TensorRT-LLM hosting a small
   model (Qwen2.5 3B AWQ or Llama 3.2 3B FP8) on the RTX 4060.
   Wire Astraea to it via the existing OpenAI-compatible HTTP
   client. End-to-end on-device demo. Covers the optimised-
   inference toolchain (engine build, kernel autotuning, KV-cache,
   paged attention) — concepts transfer to Apple's MPSGraph /
   CoreML / MLX in August.
7. **NPU learning sprint:** ONNX-RT VitisAI EP for the reranker
   on the XDNA 2 NPU. Three-way benchmark on the **same ONNX
   model**: CPU (Highway / AVX-512 via ORT CPU EP) vs. GPU (CUDA
   EP on RTX 4060) vs. NPU (VitisAI EP). Compare latency and
   power draw. This three-way comparison on one machine is rare
   and is the strongest single learning artefact in the July
   window.
8. (Optional, time permitting) Hand-written CUDA kernel for the
   reranker's softmax. Pure learning; throw away after measuring.

**August onwards (M4 Pro, ARM + Metal/CoreML/SME):**

8. CI all-green on macOS-ARM (should already be passing from
   step 3's cross-build discipline).
9. ONNX-RT-CoreML reranker in-proc. Benchmark vs HTTP reranker on
   the same machine. Compare numbers to the §5 CUDA result on
   Ryzen — same model, different silicon.
10. In-process embedder via CoreML, using the 48 GB unified memory
    headroom. Measure end-to-end request latency.
11. Local LLM via llama.cpp Metal sidecar. Wire Astraea to it.
    Now the whole stack runs on one machine — a milestone of its
    own.
12. **SME learning sprint:** re-implement the reranker's matmul
    using `<arm_sme.h>`. Benchmark against BNNS, against CoreML,
    against the CPU Highway baseline.
13. (Optional) MLX C++ exploration — re-implement one small piece
    of the pipeline (e.g., the embedder's pooling layer) directly
    in MLX. Pure learning.

The differential harness (`finding.md` §2) runs throughout, on every
platform, in CI. It is what keeps any of this from drifting from the
Python reference. Without it none of the above is safe to land.

### 9.5. Hardware-locked summary

| Window | Platform | ISA | Accelerators | Memory binding | Primary novelty |
|---|---|---|---|---|---|
| Now – Jul | Ryzen AI 9 HX 370 (12c/24t, Zen 5 + Zen 5c) + RTX 4060 mobile | x86-64 Zen 5 | CUDA + Tensor cores, **XDNA 2 NPU (50 TOPS)**, **Radeon 890M iGPU (RDNA 3.5)** | 8 GB dGPU VRAM | AVX-512, hybrid-core affinity, CUDA, TensorRT, TRT-LLM, **VitisAI NPU EP** |
| Aug onwards | M4 Pro | ARM64 (P + E cores) | Metal GPU + ANE + AMX + SME | 48 GB unified | NEON, SME, MPSGraph, CoreML, MLX |

What stays the same across the two: C++23, Clang, Drogon, Glaze,
mimalloc, simdutf, Highway, spdlog, doctest, nanobench, ONNX Runtime
as the abstraction layer, the pybind11 differential harness, CMake
presets and CI matrix covering both, and the **hybrid-core CPU
affinity pattern** (Zen 5 vs Zen 5c on Strix Point maps directly to
P-cores vs E-cores on M4 — the same affinity code, with the topology
detection swapped, works for both).

What changes: the execution provider (CUDA + VitisAI → CoreML), the
SIMD codegen (AVX-512 → NEON), the available specialty accelerators
(Tensor cores + XDNA NPU → AMX + ANE + SME), and the framework
topology (external models → optionally in-process at the M4 RAM
scale).

The **three-way reranker benchmark** from §9.4 step 7 (CPU vs GPU
vs NPU on Strix Point) becomes a **four-way comparison** when
extended to M4 in August (add CoreML on Metal GPU and on ANE). One
ONNX model, four execution providers, two ISAs — that's the
artefact worth aiming the whole exercise at.
