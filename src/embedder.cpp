#include "astraea/embedder.hpp"
#include "astraea/detail/when_all.hpp"
#include <glaze/glaze.hpp>
#include <stdexcept>
#include <trantor/net/EventLoop.h>

// JSON structs must have external linkage for glaze reflection.
// Anonymous-namespace types are TU-local ([basic.link]) and break
// glz::detail::external<T> on Clang 18 + libc++.
namespace astraea::detail::embedder_json {

struct EmbedReq {
    std::string model;
    std::vector<std::string> input;
};

struct EmbedEntry {
    std::vector<float> embedding;
};

struct EmbedResp {
    std::vector<EmbedEntry> data;
};

} // namespace astraea::detail::embedder_json

namespace astraea {

namespace {

using namespace astraea::detail::embedder_json;

drogon::HttpClientPtr make_client(const std::string& url) {
    auto c = drogon::HttpClient::newHttpClient(url);
    if (!c) throw std::invalid_argument("invalid base_url: " + url);
    return c;
}

} // anonymous namespace

Embedder::Embedder(std::string base_url, std::string model, int dimensions,
                   double timeout_s)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _dimensions(dimensions)
    , _timeout_s(timeout_s)
    , _client(make_client(_base_url))
{}

drogon::Task<std::vector<float>> Embedder::embed(std::string text) const {
    using namespace astraea::detail::embedder_json;

    std::string body;
    if (auto we = glz::write_json(EmbedReq{_model, {std::move(text)}}, body); we)
        throw std::runtime_error("embed: request serialization failed");

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/v1/embeddings");
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto resp = co_await _client->sendRequestCoro(req, _timeout_s);
    if (static_cast<int>(resp->statusCode()) != 200)
        throw std::runtime_error("embed: HTTP " +
                                 std::to_string(static_cast<int>(resp->statusCode())));

    EmbedResp parsed{};
    if (auto pe = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, resp->body()); pe)
        throw std::runtime_error("embed: response parse failed: " +
                                 glz::format_error(pe, resp->body()));
    if (parsed.data.empty())
        throw std::runtime_error("embed: empty data array in response");

    co_return std::move(parsed.data[0].embedding);
}

drogon::Task<std::vector<float>> Embedder::embed_synth(std::string text) const {
    // Fast path: cache hit under shared lock (the common case after warm()).
    {
        std::shared_lock lock(_mu);
        if (auto it = _cache.find(text); it != _cache.end())
            co_return it->second;
    }

    // Slow path: try to become the inflight owner under exclusive lock.
    // Re-check the cache first — a concurrent winner may have inserted already.
    bool i_am_owner = false;
    {
        std::unique_lock lock(_mu);
        if (auto it = _cache.find(text); it != _cache.end())
            co_return it->second;
        i_am_owner = _inflight.insert(text).second; // true if we inserted
    }

    if (!i_am_owner) {
        // Another coroutine is fetching the same key. Yield until it inserts
        // into _cache (typically one embed round-trip, 30-100 ms).
        struct Sleeper {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h) noexcept {
                trantor::EventLoop::getEventLoopOfCurrentThread()
                    ->runAfter(0.01 /*10ms*/, [h]() mutable { h.resume(); });
            }
            void await_resume() noexcept {}
        };
        for (int i = 0; i < 500; ++i) { // up to ~5 s
            co_await Sleeper{};
            std::shared_lock lock(_mu);
            if (auto it = _cache.find(text); it != _cache.end())
                co_return it->second;
        }
        // Timeout: the owner likely threw. Fall back to fetching ourselves.
        co_return co_await embed(text);
    }

    // We are the inflight owner — fetch and insert, then clear inflight.
    try {
        auto vec = co_await embed(text);
        {
            std::unique_lock lock(_mu);
            _cache.emplace(text, vec);
            _inflight.erase(text);
        }
        co_return std::move(vec);
    } catch (...) {
        std::unique_lock lock(_mu);
        _inflight.erase(text);
        throw;
    }
}

drogon::Task<> Embedder::warm(std::vector<std::string> queries) {
    std::vector<drogon::Task<>> tasks;
    tasks.reserve(queries.size());
    for (const auto& q : queries) {
        tasks.push_back(([this, q]() -> drogon::Task<> {
            co_await embed_synth(q);
        })());
    }
    co_await detail::when_all_void(std::move(tasks));
}

} // namespace astraea
