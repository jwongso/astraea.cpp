#include "astraea/session.hpp"
#include "astraea/detail/redis_url.hpp"

#include <glaze/glaze.hpp>
#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>
#include <trantor/net/EventLoop.h>

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace astraea {

namespace {

// Per-thread hiredis context. Same pattern as RedisCoordinator: lazy init,
// freed at thread exit, reconnects on first command error.
struct ThreadLocalCtx {
    redisContext* ctx = nullptr;
    ~ThreadLocalCtx() {
        if (ctx) { redisFree(ctx); ctx = nullptr; }
    }
};
thread_local ThreadLocalCtx tl_ctx;

redisContext* get_thread_context(const std::string& host, int port, int db) {
    auto& tc = tl_ctx;
    if (tc.ctx && !tc.ctx->err) return tc.ctx;
    if (tc.ctx) { redisFree(tc.ctx); tc.ctx = nullptr; }

    redisContext* c = redisConnect(host.c_str(), port);
    if (!c) throw std::runtime_error("SessionStore: redisConnect returned null");
    if (c->err) {
        std::string msg = "SessionStore: connect failed: ";
        msg += c->errstr;
        redisFree(c);
        throw std::runtime_error(msg);
    }
    if (db != 0) {
        auto* r = static_cast<redisReply*>(redisCommand(c, "SELECT %d", db));
        if (!r || c->err) {
            if (r) freeReplyObject(r);
            redisFree(c);
            throw std::runtime_error("SessionStore: SELECT db failed");
        }
        freeReplyObject(r);
    }
    tc.ctx = c;
    return tc.ctx;
}

// WorkerAwaiter: same pattern as RedisCoordinator - offloads a callable to
// the thread pool and resumes the coroutine on its original event loop.
template<typename F>
struct WorkerAwaiter {
    SessionStore::ThreadPool& pool;
    F                          func;
    using R = std::invoke_result_t<F>;
    std::optional<R>           result;
    std::exception_ptr         err;
    trantor::EventLoop*        loop = nullptr;

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
            if (loop) loop->queueInLoop([h]() { h.resume(); });
            else      h.resume();
        });
    }
    R await_resume() {
        if (err) std::rethrow_exception(err);
        if constexpr (!std::is_void_v<R>) return std::move(*result);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// ThreadPool
// ---------------------------------------------------------------------------

SessionStore::ThreadPool::ThreadPool(int n_threads) {
    _workers.reserve(static_cast<std::size_t>(n_threads));
    for (int i = 0; i < n_threads; ++i)
        _workers.emplace_back([this]() { worker_loop(); });
}

SessionStore::ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _stop = true;
    }
    _cv.notify_all();
    for (auto& t : _workers)
        if (t.joinable()) t.join();
}

void SessionStore::ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _queue.push_back(std::move(task));
    }
    _cv.notify_one();
}

void SessionStore::ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [this]() { return _stop || !_queue.empty(); });
            if (_stop && _queue.empty()) return;
            task = std::move(_queue.front());
            _queue.pop_front();
        }
        try { task(); }
        catch (const std::exception& e) {
            SPDLOG_ERROR("SessionStore: worker threw: {}", e.what());
        } catch (...) {
            SPDLOG_ERROR("SessionStore: worker threw unknown exception");
        }
    }
}

// ---------------------------------------------------------------------------
// SessionStore
// ---------------------------------------------------------------------------

SessionStore::SessionStore(std::string redis_url,
                           std::string jurisdiction,
                           int ttl_seconds,
                           int max_turns,
                           int worker_threads)
    : _jurisdiction(std::move(jurisdiction))
    , _ttl_seconds(ttl_seconds)
    , _max_turns(max_turns)
    , _pool(worker_threads > 0 ? worker_threads : 2)
{
    auto parsed = detail::parse_redis_url(redis_url);
    _host = std::move(parsed.host);
    _port = parsed.port;
    _db   = parsed.db;
}

SessionStore::~SessionStore() = default;

std::string SessionStore::make_key(const std::string& session_id) const {
    return "astraea:session:" + _jurisdiction + ":" + session_id;
}

template<typename F>
auto SessionStore::run_on_worker(F&& f) {
    return WorkerAwaiter<F>{_pool, std::forward<F>(f), {}, {}, nullptr};
}

std::string SessionStore::get_sync(const std::string& key) {
    auto* ctx = get_thread_context(_host, _port, _db);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx, "GET %s", key.c_str()));
    if (!reply || ctx->err) {
        if (reply) freeReplyObject(reply);
        throw std::runtime_error("SessionStore: GET failed: " +
            std::string(ctx->errstr[0] ? ctx->errstr : "unknown"));
    }
    std::string val;
    if (reply->type == REDIS_REPLY_STRING)
        val.assign(reply->str, static_cast<std::size_t>(reply->len));
    freeReplyObject(reply);
    return val;
}

void SessionStore::setex_sync(const std::string& key, const std::string& value) {
    auto* ctx = get_thread_context(_host, _port, _db);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "SETEX %s %d %b",
            key.c_str(), _ttl_seconds,
            value.data(), value.size()));
    if (!reply || ctx->err) {
        if (reply) freeReplyObject(reply);
        throw std::runtime_error("SessionStore: SETEX failed: " +
            std::string(ctx->errstr[0] ? ctx->errstr : "unknown"));
    }
    freeReplyObject(reply);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool SessionStore::valid_session_id(const std::string& id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
            return false;
    return true;
}

drogon::Task<std::vector<ChatMessage>> SessionStore::load(const std::string& session_id) {
    std::string json;
    try {
        const auto key = make_key(session_id);
        json = co_await run_on_worker([this, &key]() { return get_sync(key); });
    } catch (const std::exception& e) {
        SPDLOG_WARN("SessionStore: load failed for {}: {}", session_id, e.what());
        co_return std::vector<ChatMessage>{};
    }

    if (json.empty()) co_return std::vector<ChatMessage>{};

    std::vector<ChatMessage> turns;
    if (auto err = glz::read_json(turns, json); err) {
        SPDLOG_WARN("SessionStore: JSON parse failed for session {}: {}",
                    session_id, glz::format_error(err, json));
        co_return std::vector<ChatMessage>{};
    }
    co_return turns;
}

drogon::Task<> SessionStore::save(const std::string& session_id,
                                  std::vector<ChatMessage> turns) {
    // Evict oldest turn pairs (user+assistant = 2 messages) when over cap.
    const int max_msgs = _max_turns * 2;
    if (static_cast<int>(turns.size()) > max_msgs) {
        turns.erase(turns.begin(),
                    turns.begin() + static_cast<int>(turns.size()) - max_msgs);
    }

    std::string json;
    if (auto err = glz::write_json(turns, json); err) {
        SPDLOG_WARN("SessionStore: JSON serialization failed for session {}", session_id);
        co_return;
    }

    try {
        const auto key = make_key(session_id);
        co_await run_on_worker([this, &key, &json]() { setex_sync(key, json); });
    } catch (const std::exception& e) {
        SPDLOG_WARN("SessionStore: save failed for {}: {}", session_id, e.what());
    }
}

} // namespace astraea
