#pragma once
//
// Shared sync-hiredis runtime: thread pool + per-thread connection cache +
// coroutine-bridging awaiter for off-loop Redis I/O.
//
// Replaces the per-component duplication that grew between RedisCoordinator
// and SessionStore (each previously had its own ThreadPool, ThreadLocalCtx,
// get_thread_context, and WorkerAwaiter<F>). A third Redis-using component
// just constructs a HiredisRuntime and calls run_on_worker.
//
// Threading model:
//   - N worker threads (configurable; usually 2-4) per runtime instance.
//   - Each worker has its own thread_local redisContext keyed by THIS
//     runtime pointer, so multiple runtimes on the same physical worker
//     thread (rare; each runtime has its own pool) don't share connections.
//   - Lazy connect on first command, reconnect on hiredis ->err.
//   - thread_local destructor frees the context when the thread exits.
//
// Lifetime:
//   - Runtime owns the pool by unique_ptr; pool's destructor joins all
//     worker threads. Tasks queued before destruction are drained.
//   - Awaiter lives in the coroutine frame and captures `this`; submit
//     -> resume -> resume cycle is safe because the coroutine frame
//     outlives any pending pool task.
//
// What this does NOT handle:
//   - TLS (rediss://). Plain TCP only, matches v1 RedisCoordinator scope.
//   - AUTH. URL parser rejects redis://...@... already.
//   - Reconnect storm protection: a permanently-down Redis means every
//     command does a fresh connect attempt. Tolerable at our op rate; the
//     fail-open semantics in callers (RedisCoordinator::acquire, Session
//     Store::load) absorb this without surfacing to clients.
//
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

struct redisContext;

namespace astraea::detail {

class HiredisRuntime {
public:
    // Connection params parsed by detail::parse_redis_url. Hold by value so
    // the runtime stays self-contained and reconnects can rebuild the
    // context from the original parameters even if the caller's URL went
    // out of scope.
    HiredisRuntime(std::string host, int port, int db, int n_threads);
    ~HiredisRuntime();

    HiredisRuntime(const HiredisRuntime&)            = delete;
    HiredisRuntime& operator=(const HiredisRuntime&) = delete;

    // Convenience factory: parses a redis://host[:port][/db] URL via
    // detail::parse_redis_url and constructs a HiredisRuntime in one step.
    // Throws std::invalid_argument on bad URLs (matches parse_redis_url).
    static HiredisRuntime from_url(const std::string& redis_url, int n_threads);

    // Run sync hiredis function `f` on a worker thread. `f` receives a
    // per-thread redisContext* that is lazily connected and reconnected on
    // hiredis error. The awaiter resumes the coroutine on its original
    // event loop with f's return value (or rethrows on exception).
    //
    // Signature of f: R(redisContext*)
    // R may be void.
    template<typename F>
    auto run_on_worker(F&& f);

    // Fire-and-forget submit: same dispatch onto a worker thread, but
    // no awaiter, no resume, no exception propagation. Use for release
    // paths called from RAII destructors that cannot block or throw.
    // Exceptions inside `f` are logged and swallowed.
    template<typename F>
    void submit_void(F&& f);

    // For tests / introspection.
    int worker_count() const noexcept;

private:
    // Lightweight thread pool. Public only to the awaiter template via
    // friend; not exposed in the header API surface.
    class ThreadPool {
    public:
        explicit ThreadPool(int n_threads);
        ~ThreadPool();
        ThreadPool(const ThreadPool&)            = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        void submit(std::function<void()> task);
        int  size() const noexcept { return static_cast<int>(_workers.size()); }

    private:
        void worker_loop();
        std::vector<std::thread>          _workers;
        std::deque<std::function<void()>> _queue;
        std::mutex                        _mu;
        std::condition_variable           _cv;
        bool                              _stop = false;
    };

    // Resolve (or lazily create) this thread's redisContext for THIS
    // runtime. Throws std::runtime_error on connect failure - callers wrap
    // and apply their own fail-open semantics.
    redisContext* get_thread_context();

    std::string                 _host;
    int                         _port;
    int                         _db;
    std::unique_ptr<ThreadPool> _pool;

    // The WorkerAwaiter template needs to see ThreadPool to type its `pool`
    // member, and get_thread_context to fetch the per-thread context.
    template<typename F> friend struct WorkerAwaiter;
};

// Awaiter: dispatches `func(ctx)` to the runtime's pool, then resumes the
// coroutine on its original event loop with the result. Move-only state
// lives in the coroutine frame for the full submit -> resume cycle.
template<typename F>
struct WorkerAwaiter {
    HiredisRuntime* runtime;
    F               func;
    using R = std::invoke_result_t<F, redisContext*>;
    // std::optional<void> is ill-formed - use monostate as a zero-size
    // stand-in for the void-returning path so the template still compiles.
    using Storage = std::conditional_t<std::is_void_v<R>, std::monostate, std::optional<R>>;
    Storage             result;
    std::exception_ptr  err;
    trantor::EventLoop* loop = nullptr;

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        runtime->_pool->submit([this, h]() {
            try {
                redisContext* ctx = runtime->get_thread_context();
                if constexpr (std::is_void_v<R>) {
                    func(ctx);
                } else {
                    result.emplace(func(ctx));
                }
            } catch (...) {
                err = std::current_exception();
            }
            if (loop) loop->queueInLoop([h]() { h.resume(); });
            else      h.resume();
        });
    }

    R await_resume() {
        if (err) std::rethrow_exception(err);
        if constexpr (!std::is_void_v<R>) return std::move(*result);
    }
};

template<typename F>
auto HiredisRuntime::run_on_worker(F&& f) {
    return WorkerAwaiter<std::decay_t<F>>{this, std::forward<F>(f), {}, {}, nullptr};
}

template<typename F>
void HiredisRuntime::submit_void(F&& f) {
    // Fire-and-forget. shared_ptr capture: F may be move-only, but the
    // std::function the pool submits requires copy-constructible.
    //
    // No inner try/catch: ThreadPool::worker_loop wraps every task in its
    // own try/catch and logs SPDLOG_ERROR for any throw. Swallowing here
    // would silently lose the diagnostic.
    using Fn = std::decay_t<F>;
    auto fp = std::make_shared<Fn>(std::forward<F>(f));
    HiredisRuntime* self = this;
    _pool->submit([self, fp]() {
        redisContext* ctx = self->get_thread_context();
        (*fp)(ctx);
    });
}

} // namespace astraea::detail
