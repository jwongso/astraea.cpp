#pragma once
//
// CoordinatorClient backed by Redis for cross-process / multi-host coordination.
//
// Use when multiple nz_tenancy binaries share a single LLM server (multi-host
// or multi-container deployment). The InProcessCoordinator is fine for the
// single-binary case; this backend is what makes LLM_GLOBAL_CONCURRENCY a
// real cluster-wide cap rather than a per-binary cap.
//
// ---------------------------------------------------------------------------
// Design (option A from the PR #27 review thread: sync hiredis + thread
// offload + runAfter-driven coroutine polling)
// ---------------------------------------------------------------------------
//
//   - hiredis sync client per worker thread (hiredis sync API is not
//     thread-safe; thread_local context, lazy-init, free on thread exit).
//   - Small internal ThreadPool (default 4 workers) handles all Redis I/O
//     so coroutines never block their event loop on a Redis call.
//   - Acquire is a polling loop:
//       1. EVAL Lua script on a worker thread (returns N>=1 on grant, -1
//          on contention). This is a single Redis round-trip.
//       2. If granted: build a Permit whose destructor will DECR the counter.
//       3. If contention: schedule loop->runAfter(500 ms, resume_coro) on
//          the coroutine's original event loop and re-poll. Worker thread
//          is occupied for ~100 us per poll, NOT 500 ms - the sleep happens
//          on the loop, not the worker.
//       4. Repeat until success or timeout deadline.
//   - Lua script is Python-parity verbatim (core/queue.py:_LUA_ACQUIRE):
//       GET key -> if cur < max then INCR + EXPIRE ttl -> return new value
//       else return -1
//   - Failsafe TTL (180 s default) protects against crashed holders: a
//     process holding a permit that dies without releasing only stalls the
//     pool for at most one TTL window.
//   - Fail-open on Redis errors (network down, server gone): the acquire
//     returns success with an empty-release Permit. Matches Python's
//     "Redis error -> let the LLM handle it" semantics. Coordination
//     degrades gracefully to per-process caps rather than 503-ing everyone.
//
// ---------------------------------------------------------------------------
// What this does NOT do (deliberate scope for Phase 2)
// ---------------------------------------------------------------------------
//
//   - No TLS (rediss://). Cluster-private Redis only.
//   - No AUTH (REDIS_URL password not parsed). Same constraint.
//   - No PUBSUB notification on release - we still poll. PUBSUB would lower
//     latency under heavy contention but complicates the thread/loop model
//     for marginal benefit at our op rate.
//   - No cluster / sentinel discovery. Single Redis endpoint only.
//   - No connection pool sharing across RedisCoordinator instances - one
//     coordinator per process is assumed.
//
#include "astraea/coordinator.hpp"
#include "astraea/detail/hiredis_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace astraea {

class RedisCoordinator final : public CoordinatorClient {
public:
    // redis_url: redis://host:port[/db]  (no TLS, no AUTH in v1)
    // max_concurrency: cluster-wide cap; matches the Lua script's ARGV[1].
    // poll_interval: gap between contention re-polls.
    // ttl: failsafe key TTL refreshed on every successful acquire.
    // worker_threads: size of the internal sync-hiredis worker pool.
    RedisCoordinator(std::string redis_url,
                     int max_concurrency,
                     std::chrono::milliseconds poll_interval = std::chrono::milliseconds(500),
                     std::chrono::seconds      ttl           = std::chrono::seconds(180),
                     int                       worker_threads = 4);

    ~RedisCoordinator() override;

    RedisCoordinator(const RedisCoordinator&)            = delete;
    RedisCoordinator& operator=(const RedisCoordinator&) = delete;

    drogon::Task<Permit> acquire() override;
    drogon::Task<std::optional<Permit>> acquire(
        std::chrono::milliseconds timeout) override;

    int         max_concurrency() const noexcept override { return _max; }
    const char* backend_name()    const noexcept override { return "redis"; }

private:
    friend struct RedisPermit;

    // Atomic Lua release submitted onto a worker thread. Called from the
    // Permit destructor; never throws.
    void async_release() noexcept;

    // Sleep on the current trantor loop via runAfter, without occupying a
    // worker thread. Used between acquire polls.
    static drogon::Task<void> sleep_on_loop(std::chrono::milliseconds dur);

    int                       _max;
    std::chrono::milliseconds _poll;
    std::chrono::seconds      _ttl;
    detail::HiredisRuntime    _hiredis; // owns thread pool + per-thread contexts
};

} // namespace astraea
