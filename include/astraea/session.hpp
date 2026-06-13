#pragma once
//
// Redis-backed conversation session store.
//
// Persists turn history (user + assistant messages) across requests so the
// LLM can refer back to earlier context in the same conversation.
//
// Key pattern: astraea:session:<jurisdiction>:<session_id>
// Value:       JSON array of {role, content} objects (ChatMessage)
// TTL:         refreshed on every save (configurable, default 3600 s)
// Max turns:   older pairs are evicted when the cap is reached (default 10)
//
// Session ID arrives in the X-Session-Id request header. Requests without
// the header are stateless - session load/save is skipped entirely.
//
// Fail-open: Redis errors are logged and return an empty history on load or
// a silent no-op on save - they never propagate to the caller.
//
// Thread model: same thread-pool + thread_local hiredis context approach as
// RedisCoordinator. Sync hiredis commands are offloaded to worker threads so
// event-loop threads are never blocked.
//
#include "astraea/generator.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <drogon/utils/coroutine.h>

namespace astraea {

class SessionStore {
public:
    SessionStore(std::string redis_url,
                 std::string jurisdiction,
                 int ttl_seconds    = 3600,
                 int max_turns      = 10,
                 int worker_threads = 2);
    ~SessionStore();

    SessionStore(const SessionStore&)            = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    // Load turn history. Returns empty vector on cache miss or Redis error.
    drogon::Task<std::vector<ChatMessage>> load(const std::string& session_id);

    // Save updated turn history with TTL refresh. Older turns are evicted if
    // max_turns is exceeded. Errors are logged and silently swallowed.
    drogon::Task<> save(const std::string& session_id,
                        std::vector<ChatMessage> turns);

    // Validate a session_id from an untrusted header: must be 1-64 chars,
    // alphanumeric + hyphens only. Returns false for anything else.
    static bool valid_session_id(const std::string& id) noexcept;

private:
    struct ThreadPool {
        explicit ThreadPool(int n_threads);
        ~ThreadPool();
        void submit(std::function<void()> task);
    private:
        void worker_loop();
        std::vector<std::thread>        _workers;
        std::deque<std::function<void()>> _queue;
        std::mutex                      _mu;
        std::condition_variable         _cv;
        bool                            _stop = false;
    };

    std::string _host;
    int         _port;
    int         _db;
    std::string _jurisdiction;
    int         _ttl_seconds;
    int         _max_turns;
    ThreadPool  _pool;

    std::string make_key(const std::string& session_id) const;

    // Sync hiredis helpers - run on worker threads only.
    std::string get_sync(const std::string& key);
    void        setex_sync(const std::string& key, const std::string& value);

    template<typename F>
    auto run_on_worker(F&& f);
};

} // namespace astraea
