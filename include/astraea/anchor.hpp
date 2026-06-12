#pragma once
#include "astraea/jurisdiction.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/retriever.hpp"
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace astraea {

// Result of retrieve_anchor.
struct AnchorResult {
    std::string              anchor_text;
    std::vector<QdrantPoint> leg_sources;
    // CE gate log omitted - will be emitted via spdlog when that wiring lands.
};

// Result of retrieve_manual_guidance.
struct GuidanceResult {
    std::string              text;
    std::optional<QdrantPoint> source; // nullopt if nothing injected
    // "route_forced_vector" | "route_forced" | "vector_search" | ""
    std::string reason;
};

// Retrieve legislation sections as anchor context.
// leg_store is nullable - pass nullptr when the jurisdiction has no separate
// legislation collection. Returns an empty AnchorResult on error or nullptr.
// The caller must ensure leg_store outlives the coroutine.
drogon::Task<AnchorResult> retrieve_anchor(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    VectorStore* leg_store,
    const JurisdictionBase& jurisdiction);

// Supplementary case retrieval for matched routes with case_synthetic_query.
// Extends context_texts and sources in-place; deduplicates by QdrantPoint.id.
// No-op when no matched route carries a case_synthetic_query.
drogon::Task<> augment_case_retrieval(
    std::string question,
    std::string retrieval_question,
    RAGPipeline& pipeline,
    const JurisdictionBase& jurisdiction,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources);

// Top-1 authoritative MANUAL guidance chunk as a parallel injection.
// Route-forced docs from matched routes are injected regardless of vector score.
// Falls back to a vector threshold search when no route guidance is forced.
drogon::Task<GuidanceResult> retrieve_manual_guidance(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    const std::unordered_set<std::string>& existing_source_ids,
    const JurisdictionBase& jurisdiction);

// Second retrieval pass for low-confidence responses.
// Extends context_texts and sources in-place, re-sorts by score, caps at 6.
drogon::Task<> refine_retrieve(
    std::string original_question,
    std::string rewritten_question,
    RAGPipeline& pipeline,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources);

} // namespace astraea
