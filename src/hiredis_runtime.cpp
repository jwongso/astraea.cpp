#include "astraea/detail/hiredis_runtime.hpp"
#include "astraea/detail/redis_url.hpp"

#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace astraea::detail {

// ---------------------------------------------------------------------------
// Per-thread context cache
//
// Sync hiredis is not thread-safe so each worker thread needs its own
// redisContext per runtime instance it talks to. Two HiredisRuntime
// instances in the same process (e.g. one for the LLM coordinator, one
// for sessions) each have their own thread pool, so any individual
// worker thread only serves one runtime - a per-runtime keyed map would
// be overkill. But to keep the design clean if a runtime is ever shared
// across pools later, we key by HiredisRuntime* in a thread_local map.
//
// thread_local destructor frees all contexts owned by this thread when
// it exits, so pool shutdown cleans up cleanly with no leak.
// ---------------------------------------------------------------------------

namespace {

struct ThreadLocalCache {
    std::unordered_map<HiredisRuntime*, redisContext*> contexts;
    ~ThreadLocalCache() {
        for (auto& [_, ctx] : contexts) {
            if (ctx) redisFree(ctx);
        }
    }
};

thread_local ThreadLocalCache tl_cache;

redisContext* connect_redis(const std::string& host, int port, int db) {
    redisContext* c = redisConnect(host.c_str(), port);
    if (!c)
        throw std::runtime_error("HiredisRuntime: redisConnect returned null");
    if (c->err) {
        std::string msg = "HiredisRuntime: connect failed: ";
        msg += c->errstr;
        redisFree(c);
        throw std::runtime_error(msg);
    }
    if (db != 0) {
        auto* reply = static_cast<redisReply*>(redisCommand(c, "SELECT %d", db));
        if (!reply || c->err) {
            std::string msg = "HiredisRuntime: SELECT db failed";
            if (reply) freeReplyObject(reply);
            redisFree(c);
            throw std::runtime_error(msg);
        }
        freeReplyObject(reply);
    }
    return c;
}

} // namespace

redisContext* HiredisRuntime::get_thread_context() {
    auto& slot = tl_cache.contexts[this]; // value-initialised to nullptr
    if (slot && slot->err) {
        // Stale connection - drop and reconnect on next request.
        redisFree(slot);
        slot = nullptr;
    }
    if (!slot) slot = connect_redis(_host, _port, _db);
    return slot;
}

// ---------------------------------------------------------------------------
// ThreadPool
// ---------------------------------------------------------------------------

HiredisRuntime::ThreadPool::ThreadPool(int n_threads) {
    if (n_threads < 1) n_threads = 1;
    _workers.reserve(static_cast<std::size_t>(n_threads));
    for (int i = 0; i < n_threads; ++i)
        _workers.emplace_back([this]() { worker_loop(); });
}

HiredisRuntime::ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _stop = true;
    }
    _cv.notify_all();
    for (auto& t : _workers)
        if (t.joinable()) t.join();
}

void HiredisRuntime::ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _queue.push_back(std::move(task));
    }
    _cv.notify_one();
}

void HiredisRuntime::ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [this]() { return _stop || !_queue.empty(); });
            if (_stop && _queue.empty()) return;
            task = std::move(_queue.front());
            _queue.pop_front();
        }
        try {
            task();
        } catch (const std::exception& e) {
            SPDLOG_ERROR("HiredisRuntime: worker task threw: {}", e.what());
        } catch (...) {
            SPDLOG_ERROR("HiredisRuntime: worker task threw unknown exception");
        }
    }
}

// ---------------------------------------------------------------------------
// HiredisRuntime
// ---------------------------------------------------------------------------

HiredisRuntime::HiredisRuntime(std::string host, int port, int db, int n_threads)
    : _host(std::move(host))
    , _port(port)
    , _db(db)
    , _pool(std::make_unique<ThreadPool>(n_threads))
{}

HiredisRuntime HiredisRuntime::from_url(const std::string& redis_url, int n_threads) {
    auto parsed = parse_redis_url(redis_url);
    return HiredisRuntime(std::move(parsed.host), parsed.port, parsed.db, n_threads);
}

HiredisRuntime::~HiredisRuntime() {
    // _pool.reset() joins all worker threads. Each exiting thread runs its
    // thread_local ThreadLocalCache destructor, which calls redisFree on
    // every context in the map - so contexts owned by THIS runtime in pool
    // workers are freed implicitly via that destructor.
    //
    // Other threads (not in our pool) never call into this runtime's
    // get_thread_context, so their tl_cache.contexts[this] entries don't
    // exist - no leak there either.
    _pool.reset();
}

int HiredisRuntime::worker_count() const noexcept {
    return _pool ? _pool->size() : 0;
}

} // namespace astraea::detail
