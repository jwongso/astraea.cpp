#include "astraea/reranker.hpp"

namespace astraea {

Reranker::Reranker(std::string base_url, std::string model, bool enabled)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _enabled(enabled)
    , _client(drogon::HttpClient::newHttpClient(_base_url))
{}

drogon::Task<std::vector<RerankCandidate>> Reranker::score_and_filter(
    std::string /*query*/,
    std::vector<RerankCandidate> candidates,
    float min_score) const
{
    if (!_enabled) {
        co_return candidates;
    }

    // TODO(Phase3): POST {"model":_model,"query":query,
    //   "documents":[c.text for c in candidates]} to /v1/rerank.
    // Map scores back by index. Drop candidates below min_score unless forced.
    // On any error (timeout, bad response), fall back: return all candidates.

    std::vector<RerankCandidate> result;
    for (auto& c : candidates) {
        if (c.forced || c.score >= min_score)
            result.push_back(std::move(c));
    }
    co_return result;
}

} // namespace astraea
