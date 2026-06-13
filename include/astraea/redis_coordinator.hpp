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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

    // Lightweight thread pool. Public because the in-cpp WorkerAwaiter<F>
    // template needs to name it. It is otherwise an implementation detail -
    // do not use it from outside the coordinator (no stability guarantees).
    class ThreadPool {
    public:
        explicit ThreadPool(int n_threads);
        ~ThreadPool();
        void submit(std::function<void()> task);
    private:
        void worker_loop();
        std::vector<std::thread>           _workers;
        std::deque<std::function<void()>>  _queue;
        std::mutex                         _mu;
        std::condition_variable            _cv;
        bool                               _stop = false;
    };

private:
    friend struct RedisPermit;

    // Worker-thread synchronous primitives. Both can throw std::runtime_error
    // on Redis errors; callers wrap them and apply fail-open semantics.
    int  exec_lua_acquire_sync();           // returns >= 1 on grant, -1 on contention
    void exec_decr_release_sync() noexcept; // releases; never throws (fail-silent on Redis error)

    // Run an arbitrary function on a worker thread, await result on the
    // calling coroutine's original event loop. Internal helper for
    // bridging hiredis sync into Drogon coroutines.
    template<typename F>
    auto run_on_worker(F&& f);

    // Sleep on the current trantor loop via runAfter, without occupying a
    // worker thread. Used between acquire polls.
    static drogon::Task<void> sleep_on_loop(std::chrono::milliseconds dur);

    std::string               _host;
    int                       _port;
    int                       _db;
    int                       _max;
    std::chrono::milliseconds _poll;
    std::chrono::seconds      _ttl;
    ThreadPool                _pool;
};

} // namespace astraea
