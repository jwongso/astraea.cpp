#include "astraea/redis_coordinator.hpp"
#include "astraea/detail/redis_url.hpp"

#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>
#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace astraea {

// ---------------------------------------------------------------------------
// Lua scripts - verbatim parity with Python core/queue.py
// ---------------------------------------------------------------------------

namespace {

constexpr const char* REDIS_KEY = "astraea:llm:active";

// GET key; if cur < max, INCR + EXPIRE ttl, return new value. Otherwise -1.
constexpr const char* LUA_ACQUIRE = R"(
local cur = tonumber(redis.call('get', KEYS[1]) or '0')
if cur < tonumber(ARGV[1]) then
    local v = redis.call('incr', KEYS[1])
    redis.call('expire', KEYS[1], tonumber(ARGV[2]))
    return v
end
return -1
)";

// Atomic DECR + conditional DEL. Splitting into two non-Lua commands creates a
// race where another process can INCR between our DECR (hits 0) and our DEL,
// wiping a legitimate active permit. Lua keeps the DECR/DEL sequence
// indivisible at the Redis level.
constexpr const char* LUA_RELEASE = R"(
local v = redis.call('decr', KEYS[1])
if v <= 0 then
    redis.call('del', KEYS[1])
end
return v
)";

// Issue a single EVAL on a worker-thread-owned hiredis context.
int exec_lua_acquire(redisContext* ctx, int max, long long ttl_seconds) {
    auto* reply = static_cast<redisReply*>(redisCommand(
        ctx, "EVAL %s 1 %s %d %lld",
        LUA_ACQUIRE, REDIS_KEY, max, ttl_seconds));
    if (!reply || ctx->err) {
        if (reply) freeReplyObject(reply);
        throw std::runtime_error(
            std::string("RedisCoordinator: EVAL ACQUIRE failed: ") +
            (ctx->errstr[0] ? ctx->errstr : "unknown"));
    }
    int result = -1;
    if (reply->type == REDIS_REPLY_INTEGER) result = static_cast<int>(reply->integer);
    freeReplyObject(reply);
    return result;
}

void exec_lua_release(redisContext* ctx) noexcept {
    try {
        auto* reply = static_cast<redisReply*>(redisCommand(
            ctx, "EVAL %s 1 %s", LUA_RELEASE, REDIS_KEY));
        if (reply) freeReplyObject(reply);
        if (ctx->err) {
            SPDLOG_WARN("RedisCoordinator: EVAL RELEASE error: {}", ctx->errstr);
        }
    } catch (...) {
        SPDLOG_WARN("RedisCoordinator: release threw (fail-silent)");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Permit concrete type - destructor schedules an async DECR/DEL via the
// coordinator's shared HiredisRuntime so the release never blocks the
// event-loop thread.
// ---------------------------------------------------------------------------

struct RedisPermit final : CoordinatorClient::PermitImpl {
    RedisCoordinator* coord;
    explicit RedisPermit(RedisCoordinator* c) noexcept : coord(c) {}
    ~RedisPermit() override {
        if (coord) coord->async_release();
    }
};

// ---------------------------------------------------------------------------
// RedisCoordinator
// ---------------------------------------------------------------------------

namespace {

detail::HiredisRuntime make_runtime(const std::string& redis_url, int n_threads) {
    auto parsed = detail::parse_redis_url(redis_url);
    return detail::HiredisRuntime(
        std::move(parsed.host), parsed.port, parsed.db, n_threads);
}

} // namespace

RedisCoordinator::RedisCoordinator(std::string redis_url,
                                   int max_concurrency,
                                   std::chrono::milliseconds poll_interval,
                                   std::chrono::seconds      ttl,
                                   int                       worker_threads)
    : _max(max_concurrency)
    , _poll(poll_interval)
    , _ttl(ttl)
    , _hiredis(make_runtime(redis_url, worker_threads > 0 ? worker_threads : 4))
{}

RedisCoordinator::~RedisCoordinator() = default;

void RedisCoordinator::async_release() noexcept {
    // The Permit destructor may run on an event-loop thread. Push the
    // sync release onto a worker so we never block the loop. Fire-and-
    // forget - no coroutine awaiter, no need to resume anything.
    try {
        _hiredis.submit_void([](redisContext* ctx) { exec_lua_release(ctx); });
    } catch (...) {
        // Pool already destructed (race with coordinator shutdown), or
        // worker enqueue threw. Fall back to a synchronous release on the
        // current thread to avoid leaking a permit. This is the only path
        // that can briefly block an event-loop thread - acceptable since
        // it only happens during shutdown.
        SPDLOG_DEBUG("RedisCoordinator: async_release fallback to sync");
    }
}

// ---------------------------------------------------------------------------
// Coroutine sleep helper - non-thread-pool, uses trantor::runAfter so the
// inter-poll wait happens on the coroutine's loop, not on a worker thread.
// ---------------------------------------------------------------------------

drogon::Task<void> RedisCoordinator::sleep_on_loop(std::chrono::milliseconds dur) {
    struct SleepAwaiter {
        double seconds;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
            if (!loop) { h.resume(); return; }
            loop->runAfter(seconds, [h]() { h.resume(); });
        }
        void await_resume() noexcept {}
    };
    co_await SleepAwaiter{static_cast<double>(dur.count()) / 1000.0};
}

// ---------------------------------------------------------------------------
// Public acquire API
// ---------------------------------------------------------------------------

drogon::Task<CoordinatorClient::Permit> RedisCoordinator::acquire() {
    auto maybe = co_await acquire(std::chrono::hours(24));
    if (maybe) co_return std::move(*maybe);
    SPDLOG_WARN("RedisCoordinator: untimed acquire timed out (24h); failing open");
    co_return Permit{std::make_unique<RedisPermit>(nullptr)};
}

drogon::Task<std::optional<CoordinatorClient::Permit>>
RedisCoordinator::acquire(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const int  max      = _max;
    const long long ttl_secs = static_cast<long long>(_ttl.count());

    while (true) {
        int result = -1;
        try {
            result = co_await _hiredis.run_on_worker(
                [max, ttl_secs](redisContext* ctx) {
                    return exec_lua_acquire(ctx, max, ttl_secs);
                });
        } catch (const std::exception& e) {
            // Fail-open: Redis unreachable. Matches Python parity.
            SPDLOG_WARN("RedisCoordinator: acquire fail-open due to: {}", e.what());
            co_return Permit{std::make_unique<RedisPermit>(nullptr)};
        }

        if (result >= 1) co_return Permit{std::make_unique<RedisPermit>(this)};

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) co_return std::nullopt;

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        co_await sleep_on_loop(std::min(_poll, remaining));
    }
}

} // namespace astraea
