#include "astraea/embedder.hpp"
#include <stdexcept>

namespace astraea {

Embedder::Embedder(std::string base_url, std::string model, int dimensions)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _dimensions(dimensions)
    , _client(drogon::HttpClient::newHttpClient(_base_url))
{}

drogon::Task<std::vector<float>> Embedder::embed(std::string /*text*/) const {
    // TODO(Phase3): POST {"model":_model,"input":[text]} to /v1/embeddings,
    // parse data[0].embedding array via glaze, return float vector.
    co_return std::vector<float>(_dimensions, 0.0f);
}

drogon::Task<std::vector<float>> Embedder::embed_synth(std::string text) const {
    {
        std::shared_lock lock(_mu);
        if (auto it = _cache.find(text); it != _cache.end())
            co_return it->second;
    }
    // TODO(Phase3): TOCTOU window here - concurrent misses for the same key
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
    co_return vec;
}

drogon::Task<> Embedder::warm(std::vector<std::string> queries) {
    for (auto& q : queries)
        co_await embed_synth(q);
}

} // namespace astraea
