#include "astraea/pipeline.hpp"
#include "astraea/detail/pipeline_helpers.hpp"

namespace astraea {

// ---------------------------------------------------------------------------
// RAGPipeline
// ---------------------------------------------------------------------------

RAGPipeline::RAGPipeline(std::string qdrant_url,
                         std::string collection,
                         std::string embed_base_url,
                         std::string embed_model,
                         std::string llm_base_url,
                         std::string llm_model,
                         std::string rerank_base_url,
                         std::string rerank_model,
                         std::string court_name,
                         int embed_dims,
                         int llm_max_tokens,
                         float llm_temperature,
                         bool enable_reranker,
                         bool enable_thinking)
    : _embedder(std::move(embed_base_url), std::move(embed_model), embed_dims)
    , _store(std::move(qdrant_url), std::move(collection), std::move(court_name))
    , _generator(std::move(llm_base_url), std::move(llm_model), llm_max_tokens, llm_temperature, enable_thinking)
    , _reranker(std::move(rerank_base_url), std::move(rerank_model), enable_reranker)
{}

drogon::Task<RetrieveResult> RAGPipeline::retrieve(
    std::string question,
    int top_k,
    float min_score,
    int min_chunks,
    bool use_mmr) const
{
    auto query_vector = co_await _embedder.embed(question);
    // filtered_search injects the court_name set in the VectorStore constructor.
    auto raw = co_await _store.filtered_search(std::move(query_vector), top_k * 3);

    if (raw.empty()) co_return RetrieveResult{};

    detail::apply_manual_discounts(raw);
    auto hits = detail::deduplicate(std::move(raw), top_k * 2);

    if (use_mmr)
        hits = detail::mmr_select(std::move(hits), top_k);
    else if (static_cast<int>(hits.size()) > top_k)
        hits.resize(top_k);

    if (min_score > 0.0f) {
        hits.erase(std::remove_if(hits.begin(), hits.end(),
                                  [min_score](const QdrantPoint& h) {
                                      return h.score < min_score;
                                  }),
                   hits.end());
    }
    if (static_cast<int>(hits.size()) < min_chunks)
        co_return RetrieveResult{};

    RetrieveResult result;
    result.texts.reserve(hits.size());
    result.sources.reserve(hits.size());
    for (const auto& h : hits) {
        result.texts.push_back(detail::payload_field(h, "text"));
        result.sources.push_back(h);
    }
    co_return result;
}

drogon::Task<std::vector<float>> RAGPipeline::embed(std::string text) const {
    co_return co_await _embedder.embed(std::move(text));
}

} // namespace astraea
