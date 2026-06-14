#pragma once
#include "astraea/embedder.hpp"
#include "astraea/generator.hpp"
#include "astraea/reranker.hpp"
#include "astraea/retriever.hpp"
#include <string>
#include <vector>

namespace astraea {

// Combined result from RAGPipeline::retrieve().
// texts[i] is the "text" payload field of sources[i].
struct RetrieveResult {
    std::vector<std::string> texts;
    std::vector<QdrantPoint> sources;
    double                   embed_ms = 0.0; // time spent in the embed HTTP call
};

// Orchestrates embed -> filtered_search -> manual_discount -> deduplicate
// -> mmr-or-topk -> score/chunk filter.
// Owns Embedder, VectorStore, Generator, and Reranker by value.
// anchor.hpp borrows the pipeline by reference for legislation retrieval.
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

    // embed -> filtered_search -> discount -> deduplicate -> mmr-or-topk -> filter.
    // Returns at most top_k results. Returns empty if fewer than min_chunks
    // survive the min_score gate.
    //
    // Defaults match Python core/api.py:370 / :651 verbatim. Notably:
    //   - min_score = 0.75 drops low-relevance noise chunks. Defaulting to 0.0
    //     (as we did briefly) keeps every survivor of the top_k*3 raw fetch
    //     including marginal hits, which bloats the prompt 30-70% on typical
    //     queries and is the primary suspect for the C++ vs Python LLM-phase
    //     TTFT regression documented in BENCHMARK_PERF.md.
    //   - min_chunks = 2 returns an empty result rather than a single weak hit -
    //     the LLM is better off with no context than with one noisy chunk.
    drogon::Task<RetrieveResult> retrieve(
        std::string question,
        int top_k       = 5,
        float min_score = 0.75f,
        int min_chunks  = 2,
        bool use_mmr    = false) const;

    // Embed a single text via the internal Embedder.
    drogon::Task<std::vector<float>> embed(std::string text) const;

    // Accessors for anchor.cpp. Non-const: embed_synth mutates the cache.
    Embedder&    embedder()   noexcept { return _embedder; }
    VectorStore& store()      noexcept { return _store; }
    Generator&   generator()  noexcept { return _generator; }
    Reranker&    reranker()   noexcept { return _reranker; }

private:
    Embedder    _embedder;
    VectorStore _store;
    Generator   _generator;
    Reranker    _reranker;
};

} // namespace astraea
