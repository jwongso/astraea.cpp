#include "astraea/redis_coordinator.hpp"

#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>
#include <trantor/net/EventLoop.h>

#include <atomic>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace astraea {

// ---------------------------------------------------------------------------
// Constants - verbatim parity with Python core/queue.py
// ---------------------------------------------------------------------------

namespace {

constexpr const char* REDIS_KEY = "astraea:llm:active";

// Atomic Lua: GET key; if cur < max, INCR + EXPIRE ttl, return new value.
// Otherwise return -1.
constexpr const char* LUA_ACQUIRE = R"(
local cur = tonumber(redis.call('get', KEYS[1]) or '0')
if cur < tonumber(ARGV[1]) then
    local v = redis.call('incr', KEYS[1])
    redis.call('expire', KEYS[1], tonumber(ARGV[2]))
    return v
end
return -1
)";

// Per-thread hiredis context. Sync API is NOT thread-safe so each worker
// owns its own; lazy-init on first use, freed at thread exit by the
// destructor. Reconnects on first error.
struct ThreadLocalCtx {
    redisContext* ctx = nullptr;
    ~ThreadLocalCtx() {
        if (ctx) {
            redisFree(ctx);
            ctx = nullptr;
        }
    }
};
thread_local ThreadLocalCtx tl_ctx;

// Best-effort URL parser: redis://host[:port][/db]. No AUTH, no TLS in v1.
struct ParsedRedisUrl {
    std::string host;
    int         port = 6379;
    int         db   = 0;
};

ParsedRedisUrl parse_redis_url(const std::string& url) {
    constexpr std::string_view scheme = "redis://";
    if (url.rfind(scheme, 0) != 0)
        throw std::invalid_argument("RedisCoordinator: expected redis:// URL, got: " + url);

    auto rest = std::string_view(url).substr(scheme.size());

    // Strip optional path /db
    int db = 0;
    auto slash = rest.find('/');
    std::string_view authority = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    if (slash != std::string_view::npos) {
        auto db_str = rest.substr(slash + 1);
        if (!db_str.empty()) {
            int v = 0;
            for (char c : db_str) {
                if (c < '0' || c > '9') {
                    throw std::invalid_argument(
                        "RedisCoordinator: non-numeric db in URL: " + url);
                }
                v = v * 10 + (c - '0');
            }
            db = v;
        }
    }

    // No AUTH parsing - if a password is present, reject so we fail loud
    // rather than silently dropping it.
    if (authority.find('@') != std::string_view::npos) {
        throw std::invalid_argument(
            "RedisCoordinator: REDIS_URL with auth (user:pass@host) is not supported in v1: " + url);
    }

    ParsedRedisUrl out;
    auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        out.host.assign(authority);
        out.port = 6379;
    } else {
        out.host.assign(authority.substr(0, colon));
        int p = 0;
        for (char c : authority.substr(colon + 1)) {
            if (c < '0' || c > '9') {
                throw std::invalid_argument(
                    "RedisCoordinator: non-numeric port in URL: " + url);
            }
            p = p * 10 + (c - '0');
            if (p > 65535)
                throw std::invalid_argument(
                    "RedisCoordinator: port out of range in URL: " + url);
        }
        out.port = p;
    }
    if (out.host.empty())
        throw std::invalid_argument("RedisCoordinator: empty host in URL: " + url);
    out.db = db;
    return out;
}

redisContext* connect_redis(const std::string& host, int port, int db) {
    redisContext* c = redisConnect(host.c_str(), port);
    if (!c)
        throw std::runtime_error("RedisCoordinator: redisConnect returned null");
    if (c->err) {
        std::string msg = "RedisCoordinator: connect failed: ";
        msg += c->errstr;
        redisFree(c);
        throw std::runtime_error(msg);
    }
    if (db != 0) {
        auto* reply = static_cast<redisReply*>(redisCommand(c, "SELECT %d", db));
        if (!reply || c->err) {
            std::string msg = "RedisCoordinator: SELECT db failed";
            if (reply) freeReplyObject(reply);
            redisFree(c);
            throw std::runtime_error(msg);
        }
        freeReplyObject(reply);
    }
    return c;
}

redisContext* get_thread_context(const std::string& host, int port, int db) {
    if (tl_ctx.ctx && tl_ctx.ctx->err) {
        // Drop the broken context; reconnect below.
        redisFree(tl_ctx.ctx);
        tl_ctx.ctx = nullptr;
    }
    if (!tl_ctx.ctx) tl_ctx.ctx = connect_redis(host, port, db);
    return tl_ctx.ctx;
}

} // namespace

// ---------------------------------------------------------------------------
// ThreadPool
// ---------------------------------------------------------------------------

RedisCoordinator::ThreadPool::ThreadPool(int n_threads) {
    _workers.reserve(static_cast<std::size_t>(n_threads));
    for (int i = 0; i < n_threads; ++i)
        _workers.emplace_back([this]() { worker_loop(); });
}

RedisCoordinator::ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _stop = true;
    }
    _cv.notify_all();
    for (auto& t : _workers) {
        if (t.joinable()) t.join();
    }
}

void RedisCoordinator::ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _queue.push_back(std::move(task));
    }
    _cv.notify_one();
}

void RedisCoordinator::ThreadPool::worker_loop() {
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
            SPDLOG_ERROR("RedisCoordinator: worker task threw: {}", e.what());
        } catch (...) {
            SPDLOG_ERROR("RedisCoordinator: worker task threw unknown exception");
        }
    }
}

// ---------------------------------------------------------------------------
// RedisCoordinator
// ---------------------------------------------------------------------------

RedisCoordinator::RedisCoordinator(std::string redis_url,
                                   int max_concurrency,
                                   std::chrono::milliseconds poll_interval,
                                   std::chrono::seconds      ttl,
                                   int                       worker_threads)
    : _max(max_concurrency)
    , _poll(poll_interval)
    , _ttl(ttl)
    , _pool(worker_threads > 0 ? worker_threads : 4)
{
    auto parsed = parse_redis_url(redis_url);
    _host = std::move(parsed.host);
    _port = parsed.port;
    _db   = parsed.db;
}

RedisCoordinator::~RedisCoordinator() = default;

int RedisCoordinator::exec_lua_acquire_sync() {
    auto* ctx = get_thread_context(_host, _port, _db);
    auto* reply = static_cast<redisReply*>(redisCommand(
        ctx, "EVAL %s 1 %s %d %lld",
        LUA_ACQUIRE,
        REDIS_KEY,
        _max,
        static_cast<long long>(_ttl.count())));
    if (!reply || ctx->err) {
        if (reply) freeReplyObject(reply);
        throw std::runtime_error(
            std::string("RedisCoordinator: EVAL failed: ") +
            (ctx->errstr[0] ? ctx->errstr : "unknown"));
    }
    int result = -1;
    if (reply->type == REDIS_REPLY_INTEGER) result = static_cast<int>(reply->integer);
    freeReplyObject(reply);
    return result;
}

void RedisCoordinator::exec_decr_release_sync() noexcept {
    try {
        auto* ctx = get_thread_context(_host, _port, _db);
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "DECR %s", REDIS_KEY));
        if (!reply || ctx->err) {
            if (reply) freeReplyObject(reply);
            SPDLOG_WARN("RedisCoordinator: DECR failed (fail-silent)");
            return;
        }
        long long val = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
        freeReplyObject(reply);

        if (val <= 0) {
            // Cleanup the key so a stale value can't desync the counter.
            reply = static_cast<redisReply*>(redisCommand(ctx, "DEL %s", REDIS_KEY));
            if (reply) freeReplyObject(reply);
        }
    } catch (...) {
        // Release must not propagate exceptions - it's called from a Permit
        // destructor on an arbitrary thread.
        SPDLOG_WARN("RedisCoordinator: release threw (fail-silent)");
    }
}

// ---------------------------------------------------------------------------
// Coroutine helpers
// ---------------------------------------------------------------------------

// Awaiter: dispatch `func` to the RedisCoordinator's worker pool, then resume
// the coroutine on its original event loop with the result (or rethrow on
// exception). Lives in the coroutine frame so the pool task's `this` pointer
// stays valid for the full submit -> result -> resume cycle.
template<typename F>
struct WorkerAwaiter {
    RedisCoordinator::ThreadPool& pool;
    F                              func;
    using R = std::invoke_result_t<F>;
    std::optional<R>               result;
    std::exception_ptr             err;
    trantor::EventLoop*            loop = nullptr;

    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
        loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        pool.submit([this, h]() {
            try {
                if constexpr (std::is_void_v<R>) {
                    func();
                } else {
                    result.emplace(func());
                }
            } catch (...) {
                err = std::current_exception();
            }
            // Resume on the original loop so subsequent loop-bound work
            // (e.g. sleep_on_loop, response stream send) stays loop-affine.
            if (loop) {
                loop->queueInLoop([h]() { h.resume(); });
            } else {
                h.resume();
            }
        });
    }
    R await_resume() {
        if (err) std::rethrow_exception(err);
        if constexpr (!std::is_void_v<R>) return std::move(*result);
    }
};

template<typename F>
auto RedisCoordinator::run_on_worker(F&& f) {
    return WorkerAwaiter<F>{_pool, std::forward<F>(f), {}, {}, nullptr};
}

drogon::Task<void> RedisCoordinator::sleep_on_loop(std::chrono::milliseconds dur) {
    struct SleepAwaiter {
        double seconds;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
            if (!loop) {
                // No loop attached: cannot schedule a timer. Resume directly
                // (degrades the poll loop to a tight loop, but only happens
                // in the no-loop test context which doesn't actually poll).
                h.resume();
                return;
            }
            loop->runAfter(seconds, [h]() { h.resume(); });
        }
        void await_resume() noexcept {}
    };
    co_await SleepAwaiter{static_cast<double>(dur.count()) / 1000.0};
}

// ---------------------------------------------------------------------------
// Permit concrete type - destructor releases through the coordinator
// ---------------------------------------------------------------------------

struct RedisPermit final : CoordinatorClient::PermitImpl {
    RedisCoordinator* coord;
    explicit RedisPermit(RedisCoordinator* c) noexcept : coord(c) {}
    ~RedisPermit() override {
        if (coord) coord->exec_decr_release_sync();
    }
};

// ---------------------------------------------------------------------------
// Public acquire API
// ---------------------------------------------------------------------------

drogon::Task<CoordinatorClient::Permit> RedisCoordinator::acquire() {
    // Untimed acquire is "poll forever". Implement as timed with an
    // effectively-infinite deadline so the same code path covers both.
    auto maybe = co_await acquire(std::chrono::hours(24));
    if (maybe) co_return std::move(*maybe);
    // Should be unreachable under a 24h deadline unless Redis is gone for
    // a full day - in which case fail open with an empty Permit.
    SPDLOG_WARN("RedisCoordinator: untimed acquire timed out (24h); failing open");
    co_return Permit{std::make_unique<RedisPermit>(nullptr)};
}

drogon::Task<std::optional<CoordinatorClient::Permit>>
RedisCoordinator::acquire(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        // Try EVAL on a worker thread (sync hiredis cannot run on the
        // event-loop thread without blocking it).
        int result = -1;
        try {
            result = co_await run_on_worker([this]() { return exec_lua_acquire_sync(); });
        } catch (const std::exception& e) {
            // Fail-open: Redis is unreachable. Matches Python parity -
            // degrade to "let the LLM handle it" instead of 503-ing.
            SPDLOG_WARN("RedisCoordinator: acquire fail-open due to: {}", e.what());
            co_return Permit{std::make_unique<RedisPermit>(nullptr)};
        }

        if (result >= 1) {
            // Got a real slot; the Permit's destructor will DECR.
            co_return Permit{std::make_unique<RedisPermit>(this)};
        }

        // Contention. Check deadline before sleeping so we don't sleep
        // past it.
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) co_return std::nullopt;

        // Sleep on the coroutine's event loop (NOT on a worker thread) so
        // we don't occupy a pool slot for hundreds of milliseconds while
        // doing nothing.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto sleep_for = std::min(_poll, remaining);
        co_await sleep_on_loop(sleep_for);
    }
}

} // namespace astraea
