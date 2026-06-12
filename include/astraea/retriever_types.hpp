#pragma once
// Data-only types shared between retriever.hpp and the detail helper headers.
// No Drogon dependency - safe to include in test TUs that link only astraea_core.
#include <string>
#include <unordered_map>
#include <vector>

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

} // namespace astraea
