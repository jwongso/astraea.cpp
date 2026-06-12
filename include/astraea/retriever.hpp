#pragma once
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

// A single search result from Qdrant.
struct QdrantPoint {
    std::string id;
    float score = 0.0f;
    std::unordered_map<std::string, std::string> payload;
};

// A single payload field condition.
// When values has more than one entry, any value passes (OR within condition).
struct QdrantCondition {
    std::string field;
    std::vector<std::string> values;
};

// Qdrant filter as a conjunction of field conditions.
// All entries in must must match (AND semantics, maps to Qdrant "must" array).
struct QdrantFilter {
    std::vector<QdrantCondition> must;
};

// Qdrant REST client. One persistent HttpClientPtr per VectorStore instance.
// Mirrors Python VectorStore: search, filtered_search, fetch.
class VectorStore {
public:
    VectorStore(std::string qdrant_url,
                std::string collection,
                std::string court_name = "");

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

    // Fetch points by ID without re-scoring.
    drogon::Task<std::vector<QdrantPoint>> fetch(
        std::vector<std::string> ids) const;

    const std::string& collection() const noexcept { return _collection; }
    const std::string& court_name() const noexcept { return _court_name; }

private:
    std::string _url;
    std::string _collection;
    std::string _court_name;
    drogon::HttpClientPtr _client;
};

} // namespace astraea
