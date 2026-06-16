#pragma once
#include <memory>
#include <string>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

/// @brief A single candidate submitted to the cross-encoder for re-ranking.
struct RerankCandidate {
    std::string id; ///< Qdrant point ID; used to match the reranker output back to the original hit.
    std::string text; ///< The chunk text sent to the cross-encoder as the passage.
    float score = 0.0f; ///< Updated in-place with the cross-encoder relevance score after reranking.
    bool forced = false; ///< When true this candidate bypasses the score gate and is always included (route-forced sections).
};

/// @brief Cross-encoder reranker backed by a llama-server /v1/rerank endpoint.
///
/// Falls back gracefully by returning all candidates unfiltered when:
/// enabled is false, the endpoint is unreachable, or the response is malformed.
/// Forced candidates always appear in the output regardless of score.
class Reranker {
public:
    Reranker(std::string base_url,
             std::string model,
             bool enabled = true,
             double timeout_s = 60.0);

    /// @brief Score all candidates against `query` and drop those below min_score.
    ///
    /// Forced candidates bypass the score gate and are always included.
    /// Returns all candidates unfiltered on any error (fail-open semantics).
    drogon::Task<std::vector<RerankCandidate>> score_and_filter(
        std::string query,
        std::vector<RerankCandidate> candidates,
        float min_score = 0.15f) const;

    bool enabled() const noexcept { return _enabled; } ///< Whether reranking is active; false means score_and_filter returns candidates unchanged.

private:
    std::string _base_url;
    std::string _model;
    bool _enabled;
    double _timeout_s;
    drogon::HttpClientPtr _client;
};

} // namespace astraea
