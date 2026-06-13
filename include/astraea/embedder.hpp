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

// Async embedder backed by llama-server /v1/embeddings.
// One persistent HttpClientPtr with keep-alive - no TCP handshake per call.
// Synthetic query cache is populated at startup via warm() and read under
// shared_mutex so concurrent embed_synth() calls never block each other.
class Embedder {
public:
    Embedder(std::string base_url, std::string model, int dimensions = 768,
             double timeout_s = 30.0);

    // Embed a single text. Returns a float vector of length `dimensions`.
    drogon::Task<std::vector<float>> embed(std::string text) const;

    // Embed with cache lookup. Hits never go to the network.
    drogon::Task<std::vector<float>> embed_synth(std::string text) const;

    // Populate the synth cache at startup. Call once before serving requests.
    drogon::Task<> warm(std::vector<std::string> queries);

    int dimensions() const noexcept { return _dimensions; }

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
