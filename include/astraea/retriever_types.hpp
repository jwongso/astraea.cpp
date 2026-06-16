#pragma once
// Data-only types shared between retriever.hpp and the detail helper headers.
// No Drogon dependency - safe to include in test TUs that link only astraea_core.
#include <string>
#include <unordered_map>
#include <vector>

namespace astraea {

/// @brief A single scored search result from Qdrant.
///
/// payload maps every stored field name to its string value. All payload
/// values in astraea are stored and retrieved as strings, so the map is
/// always string->string.
struct QdrantPoint {
    std::string id; ///< Qdrant UUID for this point; used as the deduplication key.
    float score = 0.0f; ///< Similarity score returned by Qdrant; higher is more relevant.
    std::unordered_map<std::string, std::string> payload; ///< All payload fields as string key-value pairs.
};

/// @brief A single payload field condition for a Qdrant filter.
///
/// When values has more than one entry the condition is satisfied if the
/// payload field equals any one of them (OR semantics within the condition).
struct QdrantCondition {
    std::string field; ///< Name of the payload field to match against, e.g. "court_name".
    std::vector<std::string> values; ///< Accepted values; one match is sufficient.
};

/// @brief Qdrant filter expressed as a conjunction of field conditions.
///
/// All entries in must must match (AND semantics, maps to the Qdrant "must"
/// array in the REST API). Empty must means no filtering (accept all points).
struct QdrantFilter {
    std::vector<QdrantCondition> must; ///< All conditions that must hold; AND semantics.
};

} // namespace astraea
