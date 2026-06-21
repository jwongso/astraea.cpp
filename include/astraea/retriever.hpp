#pragma once
#include "astraea/retriever_types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

/// @brief Input for a single slot in VectorStore::batch_search().
struct BatchSearchRequest {
    std::vector<float>          vector; ///< Pre-computed query embedding of length Config::embed_dims.
    int                         top_k; ///< Maximum results to return for this slot.
    float                       min_score = 0.0f; ///< Results below this score are discarded.
    std::optional<QdrantFilter> filter; ///< Optional payload filter applied server-side.
};

/// @brief Async Qdrant REST client scoped to a single collection.
///
/// One persistent HttpClientPtr per instance with keep-alive so repeated
/// searches do not incur TCP handshake overhead. Mirrors Python VectorStore:
/// search, filtered_search, fetch, batch_search.
///
/// All methods are coroutine-safe and may be called concurrently from multiple
/// Drogon event-loop threads; the underlying Drogon HttpClient is thread-safe.
class VectorStore {
public:
    VectorStore(std::string qdrant_url,
                std::string collection,
                std::string court_name = "",
                double timeout_s = 30.0);

    /// @brief Vector similarity search with optional payload filter.
    /// @return Points sorted by score descending, capped at top_k, all with score >= min_score.
    drogon::Task<std::vector<QdrantPoint>> search(
        std::vector<float> query_vector,
        int top_k,
        float min_score = 0.0f,
        std::optional<QdrantFilter> filter = std::nullopt) const;

    /// @brief Vector search with the construction-time court_name ANDed into the filter.
    ///
    /// If court_name is set on this VectorStore, it is always injected as a must
    /// condition. extra_filter conditions are appended to the same must array.
    /// When court_name is empty behaves identically to search().
    drogon::Task<std::vector<QdrantPoint>> filtered_search(
        std::vector<float> query_vector,
        int top_k,
        float min_score = 0.0f,
        std::optional<QdrantFilter> extra_filter = std::nullopt) const;

    /// @brief Execute multiple searches in a single HTTP round-trip.
    ///
    /// Uses Qdrant's /points/search/batch endpoint. result[i] corresponds to
    /// requests[i]. Falls back to sequential search() calls if requests is empty.
    drogon::Task<std::vector<std::vector<QdrantPoint>>> batch_search(
        std::vector<BatchSearchRequest> requests) const;

    /// @brief Fetch points by ID without re-scoring.
    ///
    /// Useful for injecting forced_sections by ID without a vector query.
    drogon::Task<std::vector<QdrantPoint>> fetch(
        std::vector<std::string> ids) const;

    const std::string& url()        const noexcept { return _url; }
    const std::string& collection() const noexcept { return _collection; }
    const std::string& court_name() const noexcept { return _court_name; }

private:
    std::string _url;
    std::string _collection;
    std::string _court_name;
    double _timeout_s;
    drogon::HttpClientPtr _client;
};

} // namespace astraea
