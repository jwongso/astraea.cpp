#pragma once
#include "astraea/retriever_types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

// Input for a single slot in VectorStore::batch_search().
struct BatchSearchRequest {
    std::vector<float>          vector;
    int                         top_k;
    float                       min_score = 0.0f;
    std::optional<QdrantFilter> filter;
};

// Qdrant REST client. One persistent HttpClientPtr per VectorStore instance.
// Mirrors Python VectorStore: search, filtered_search, fetch, batch_search.
class VectorStore {
public:
    VectorStore(std::string qdrant_url,
                std::string collection,
                std::string court_name = "",
                double timeout_s = 30.0);

    // Vector similarity search with optional payload filter.
    drogon::Task<std::vector<QdrantPoint>> search(
        std::vector<float> query_vector,
        int top_k,
        float min_score = 0.0f,
        std::optional<QdrantFilter> filter = std::nullopt) const;

    // Search with court_name AND extra conditions ANDed together.
    // If court_name is set, it is always injected as a must condition.
    // extra_filter conditions are appended to the same must array.
    drogon::Task<std::vector<QdrantPoint>> filtered_search(
        std::vector<float> query_vector,
        int top_k,
        float min_score = 0.0f,
        std::optional<QdrantFilter> extra_filter = std::nullopt) const;

    // Execute multiple searches in a single HTTP round-trip using Qdrant's
    // /points/search/batch endpoint. result[i] corresponds to requests[i].
    // Falls back to sequential search() calls if requests is empty.
    drogon::Task<std::vector<std::vector<QdrantPoint>>> batch_search(
        std::vector<BatchSearchRequest> requests) const;

    // Fetch points by ID without re-scoring.
    drogon::Task<std::vector<QdrantPoint>> fetch(
        std::vector<std::string> ids) const;

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
