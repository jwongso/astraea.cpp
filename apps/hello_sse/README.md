# hello_sse — Drogon streaming spike

Smallest possible Drogon app that proves the async-coroutine + Server-Sent
Events spine works end-to-end. No business logic, no LLM, no routing — just
two handlers:

| Endpoint        | Behaviour                                                                  |
|-----------------|----------------------------------------------------------------------------|
| `GET /health`   | Returns `ok\n`.                                                            |
| `GET /sse/hello`| Streams `data: chunk N\n\n` for N = 0..19 at 200 ms intervals, then `data: [DONE]\n\n`, then closes. |

## Build

The app is gated by `ASTRAEA_BUILD_APPS` (default ON) **and** `ASTRAEA_BUILD_CLIENTS`
(default ON), because it transitively pulls in Drogon via `astraea_clients`.

```
cmake --preset dev
cmake --build --preset dev -t hello_sse
./build-dev/apps/hello_sse/hello_sse
```

Or release:

```
cmake --preset prod
cmake --build --preset prod -t hello_sse
./build-prod/apps/hello_sse/hello_sse
```

## Test from another terminal

```
curl -N http://localhost:8080/sse/hello
```

`-N` disables curl's output buffering so each chunk appears in real time.
You should see one `data: chunk N` line every 200 ms, then `data: [DONE]`.

```
curl http://localhost:8080/health
```

Should return `ok`.

## Config (env vars)

| Variable                    | Default | Effect                       |
|-----------------------------|---------|------------------------------|
| `ASTRAEA_HELLO_SSE_PORT`    | 8080    | TCP port to listen on        |
| `ASTRAEA_HELLO_SSE_THREADS` | 2       | Drogon event-loop thread pool size |

## What this validates

* Drogon's `newAsyncStreamResponse` with a `ResponseStreamPtr` callback
  works under the chosen build (Clang 18 + CPM-fetched Drogon v1.9.7).
* `drogon::async_run` + `drogon::sleepCoro(loop, duration)` is the right
  pattern for emitting timed chunks without blocking the event loop.
* `astraea_clients` is correctly wired as a downstream-consumable static
  library — the spike does not duplicate any Drogon CMake plumbing.
* Headers `Content-Type: text/event-stream`, `Cache-Control: no-cache`,
  and `X-Accel-Buffering: no` are sufficient for unbuffered streaming
  through any sensible reverse proxy.

## What it does not (yet) test

* Client disconnect detection mid-stream — Drogon's `ResponseStream`
  should report a closed connection; the real request handler will need to
  cancel the upstream LLM call when that happens. Out of scope for this
  spike; will be exercised when the real Phase 6 API handler is wired in.
* Per-connection back-pressure — the artificial 200 ms cadence is far
  slower than any sane consumer's socket drain rate, so back-pressure
  never engages here.
* TLS — listener is plaintext HTTP. Real deployments terminate TLS at the
  reverse proxy.
