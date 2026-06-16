#pragma once
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

/// @brief Async text embedder backed by a llama-server /v1/embeddings endpoint.
///
/// One persistent HttpClientPtr with keep-alive - no TCP handshake per call.
/// The synthetic query cache is populated at startup via warm() and read under
/// a shared_mutex so concurrent embed_synth() calls never block each other.
/// Thread-safe: all public methods may be called from multiple Drogon loops
/// simultaneously.
class Embedder {
public:
    Embedder(std::string base_url, std::string model, int dimensions = 768,
             double timeout_s = 30.0);

    /// @brief Embed a single text string.
    /// @return Float vector of length `dimensions`.
    drogon::Task<std::vector<float>> embed(std::string text) const;

    /// @brief Embed with cache lookup; cache hits never touch the network.
    ///
    /// Used for pre-embedded synthetic queries that are shared across all
    /// requests. The cache is populated by warm() at startup.
    drogon::Task<std::vector<float>> embed_synth(std::string text) const;

    /// @brief Populate the synth cache by embedding all queries in parallel.
    ///
    /// Call once during startup before serving requests. Not thread-safe with
    /// concurrent embed_synth() calls; must complete before request handlers run.
    drogon::Task<> warm(std::vector<std::string> queries);

    int dimensions() const noexcept { return _dimensions; } ///< Embedding vector dimensionality configured at construction time.

private:
    std::string _base_url;
    std::string _model;
    int _dimensions;
    double _timeout_s;
    drogon::HttpClientPtr _client;

    mutable std::shared_mutex _mu;
    mutable std::unordered_map<std::string, std::vector<float>> _cache;
};

} // namespace astraea
