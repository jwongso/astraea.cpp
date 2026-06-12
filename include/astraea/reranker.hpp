#pragma once
#include <memory>
#include <string>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

// A single candidate for re-ranking.
struct RerankCandidate {
    std::string id;
    std::string text;
    float score = 0.0f;
    // Forced candidates always pass regardless of score (route-forced sections).
    bool forced = false;
};

// Cross-encoder reranker backed by llama-server /v1/rerank.
// Falls back gracefully (returns all candidates unfiltered) when:
//   - enabled is false
//   - the endpoint is unreachable
//   - the response is malformed
// Forced candidates always appear in the output regardless of score.
class Reranker {
public:
    Reranker(std::string base_url,
             std::string model,
             bool enabled = true);

    // Score candidates and drop those below min_score.
    // Forced candidates bypass the score gate and are always included.
    drogon::Task<std::vector<RerankCandidate>> score_and_filter(
        std::string query,
        std::vector<RerankCandidate> candidates,
        float min_score = 0.15f) const;

    bool enabled() const noexcept { return _enabled; }

private:
    std::string _base_url;
    std::string _model;
    bool _enabled;
    drogon::HttpClientPtr _client;
};

} // namespace astraea
