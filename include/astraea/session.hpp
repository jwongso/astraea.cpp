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
// Inject:      caps how many of the stored pairs are returned for prompt
//              injection (default 3). Older pairs remain in Redis but never
//              hit the LLM context window. Mirrors Python core/session.py.
// Answer cap:  assistant messages are truncated to N chars at save time so
//              one long generation cannot blow the per-turn prompt budget
//              for the rest of the session (default 400). 0 disables.
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
#include "astraea/detail/hiredis_runtime.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <drogon/utils/coroutine.h>

namespace astraea {

class SessionStore {
public:
    SessionStore(std::string redis_url,
                 std::string jurisdiction,
                 int ttl_seconds    = 3600,
                 int max_turns      = 10,
                 int inject_turns   = 3,
                 int answer_cap     = 400,
                 int worker_threads = 2);
    ~SessionStore();

    SessionStore(const SessionStore&)            = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    // Load turn history. Returns empty vector on cache miss or Redis error.
    drogon::Task<std::vector<ChatMessage>> load(const std::string& session_id);

    // Save updated turn history with TTL refresh. Older turns are evicted if
    // max_turns is exceeded. Assistant message content is truncated to
    // answer_cap chars when answer_cap > 0. Errors are logged and silently
    // swallowed.
    drogon::Task<> save(const std::string& session_id,
                        std::vector<ChatMessage> turns);

    // Validate a session_id from an untrusted header: must be 1-64 chars,
    // alphanumeric + hyphens only. Returns false for anything else.
    static bool valid_session_id(const std::string& id) noexcept;

    // Truncate assistant messages to answer_cap chars. Static + pure so it
    // can be exercised without Redis. user messages are untouched (the
    // current question is already in assembled.messages; only past answers
    // bloat the prompt).
    static void truncate_assistant_messages(std::vector<ChatMessage>& turns,
                                            int answer_cap) noexcept;

    // Number of trailing messages that should be injected into the LLM
    // prompt. Returns inject_turns * 2 (one user + one assistant per pair).
    // Callers slice the tail of load()'s result down to this size.
    int inject_message_count() const noexcept { return _inject_turns * 2; }

    int answer_cap() const noexcept { return _answer_cap; }

private:
    std::string make_key(const std::string& session_id) const;

    std::string             _jurisdiction;
    int                     _ttl_seconds;
    int                     _max_turns;
    int                     _inject_turns;
    int                     _answer_cap;
    detail::HiredisRuntime  _hiredis; // shared thread pool + per-thread contexts
};

} // namespace astraea
