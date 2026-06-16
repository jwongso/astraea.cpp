#pragma once
#include "astraea/embedder.hpp"
#include "astraea/generator.hpp"
#include "astraea/reranker.hpp"
#include "astraea/retriever.hpp"
#include <string>
#include <vector>

namespace astraea {

/// @brief Combined result from RAGPipeline::retrieve().
///
/// texts[i] is the "text" payload field of sources[i]; the two vectors are
/// guaranteed to be the same length and to index in parallel.
struct RetrieveResult {
    std::vector<std::string> texts; ///< Extracted text payload for each retrieved chunk, parallel to sources.
    std::vector<QdrantPoint> sources; ///< Retrieved Qdrant points with scores and full payload.
    double                   embed_ms = 0.0; ///< Wall time of the embed HTTP call in milliseconds; 0.0 when retrieve_with_vec was used.
};

/// @brief Full RAG pipeline: embed, search, discount, deduplicate, rank, and filter.
///
/// Orchestrates: embed -> filtered_search -> manual_discount -> deduplicate
/// -> mmr-or-topk -> reranker -> score/chunk filter.
///
/// Owns Embedder, VectorStore, Generator, and Reranker by value. anchor.hpp
/// borrows the pipeline by const reference for legislation retrieval.
/// Constructed once at startup and shared across all request handlers
/// by non-owning reference; all methods are coroutine-safe.
class RAGPipeline {
public:
    RAGPipeline(std::string qdrant_url,
                std::string collection,
                std::string embed_base_url,
                std::string embed_model,
                std::string llm_base_url,
                std::string llm_model,
                std::string rerank_base_url,
                std::string rerank_model,
                std::string court_name          = "",
                int embed_dims                  = 768,
                int llm_max_tokens              = 2500,
                float llm_temperature           = 0.2f,
                bool enable_reranker            = true,
                bool enable_thinking            = true,
                double upstream_timeout_s       = 30.0,
                double stream_idle_timeout_s    = 300.0);

    /// @brief Full pipeline: embed question, search, discount, deduplicate, rank, and filter.
    ///
    /// Returns at most top_k results. Returns an empty RetrieveResult when fewer
    /// than min_chunks survive the min_score gate (the LLM is better served with
    /// no context than with a single noisy chunk). Defaults match Python
    /// core/api.py:370 / :651 verbatim.
    /// @param use_mmr When true applies Maximal Marginal Relevance instead of score-sorted top-k.
    drogon::Task<RetrieveResult> retrieve(
        std::string question,
        int top_k       = 5,
        float min_score = 0.75f,
        int min_chunks  = 2,
        bool use_mmr    = false) const;

    /// @brief Same as retrieve() but accepts a pre-computed embedding, skipping the embed call.
    ///
    /// Use when the caller already embedded the question (e.g. to share the
    /// vector with retrieve_anchor for a single embed RTT). result.embed_ms is
    /// always 0.0 when this overload is used; the caller tracks embed timing.
    drogon::Task<RetrieveResult> retrieve_with_vec(
        std::vector<float> query_vector,
        int top_k       = 5,
        float min_score = 0.75f,
        int min_chunks  = 2,
        bool use_mmr    = false) const;

    /// @brief Embed a single text string via the internal Embedder.
    /// @return Float vector of length Config::embed_dims.
    drogon::Task<std::vector<float>> embed(std::string text) const;

    /// @brief Non-const accessors for anchor.cpp, which needs to mutate the embedder synth cache.
    Embedder&    embedder()   noexcept { return _embedder; }
    VectorStore& store()      noexcept { return _store; }
    Generator&   generator()  noexcept { return _generator; }
    Reranker&    reranker()   noexcept { return _reranker; }

private:
    // Shared body of retrieve() and retrieve_with_vec(): takes a pre-computed
    // embedding, runs the post-embed pipeline stages, and returns results.
    drogon::Task<RetrieveResult> retrieve_impl(
        std::vector<float> query_vector,
        int top_k, float min_score, int min_chunks, bool use_mmr) const;

    Embedder    _embedder;
    VectorStore _store;
    Generator   _generator;
    Reranker    _reranker;
};

} // namespace astraea
