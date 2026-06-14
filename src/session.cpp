#include "astraea/session.hpp"

#include <glaze/glaze.hpp>
#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace astraea {

// ---------------------------------------------------------------------------
// Sync hiredis primitives - run on HiredisRuntime worker threads. The ctx
// pointer comes from the runtime's per-thread cache; we just issue the
// command + parse the reply.
// ---------------------------------------------------------------------------

namespace {

std::string get_sync(redisContext* ctx, const std::string& key) {
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

void setex_sync(redisContext* ctx, const std::string& key,
                int ttl_seconds, const std::string& value) {
    auto* reply = static_cast<redisReply*>(redisCommand(
        ctx, "SETEX %s %d %b",
        key.c_str(), ttl_seconds,
        value.data(), value.size()));
    if (!reply || ctx->err) {
        if (reply) freeReplyObject(reply);
        throw std::runtime_error("SessionStore: SETEX failed: " +
            std::string(ctx->errstr[0] ? ctx->errstr : "unknown"));
    }
    freeReplyObject(reply);
}

} // namespace

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
    , _hiredis(detail::HiredisRuntime::from_url(
          redis_url, worker_threads > 0 ? worker_threads : 2))
{}

SessionStore::~SessionStore() = default;

std::string SessionStore::make_key(const std::string& session_id) const {
    return "astraea:session:" + _jurisdiction + ":" + session_id;
}

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
        json = co_await _hiredis.run_on_worker(
            [&key](redisContext* ctx) { return get_sync(ctx, key); });
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
        const int ttl  = _ttl_seconds;
        co_await _hiredis.run_on_worker(
            [&key, ttl, &json](redisContext* ctx) {
                setex_sync(ctx, key, ttl, json);
            });
    } catch (const std::exception& e) {
        SPDLOG_WARN("SessionStore: save failed for {}: {}", session_id, e.what());
    }
}

} // namespace astraea
