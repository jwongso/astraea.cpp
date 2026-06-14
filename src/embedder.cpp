#include "astraea/embedder.hpp"
#include <glaze/glaze.hpp>
#include <stdexcept>

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
    {
        std::shared_lock lock(_mu);
        if (auto it = _cache.find(text); it != _cache.end())
            co_return it->second;
    }
    // TODO(Phase5): TOCTOU window here - concurrent misses for the same key
    // both fall through and both call embed(). The second result is discarded
    // on insert (emplace is a no-op for an existing key), wasting 30-100 ms.
    // Fix: upgrade to unique_lock before embed() or use a pending-futures map.
    // Acceptable for v1 since warm() serialises startup and runtime collisions
    // are rare (synthetic queries are a small fixed set).
    auto vec = co_await embed(text);
    {
        std::unique_lock lock(_mu);
        _cache.emplace(std::move(text), vec);
    }
    co_return std::move(vec);
}

drogon::Task<> Embedder::warm(std::vector<std::string> queries) {
    for (auto& q : queries)
        co_await embed_synth(q);
}

} // namespace astraea
