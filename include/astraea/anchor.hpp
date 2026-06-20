#pragma once
#include "astraea/jurisdiction.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/retriever.hpp"
#include "astraea/route_table.hpp"
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace astraea {

/// @brief Minimum vector similarity score for a guidance candidate to be injected.
///
/// Promoted to the header so callers (e.g. the context_debug event in main.cpp)
/// can reference the canonical value instead of duplicating the magic number.
inline constexpr float GUIDANCE_THRESHOLD = 0.75f;

/// @brief Result of retrieve_anchor().
struct AnchorResult {
    std::string              anchor_text;  ///< Concatenated legislation section text ready to prepend to the LLM context.
    std::vector<QdrantPoint> leg_sources;  ///< The raw Qdrant points for the injected legislation chunks.
    double                   elapsed_ms = 0.0;   ///< Wall time of the retrieve_anchor() call in milliseconds.
    bool                     route_matched = false; ///< True when at least one StatuteRoute fired for this question. False means the legislation tier degraded to flat top-k with no forced sections and no allow-list filtering.
    int                      max_hits      = 0;     ///< The cap applied to legislation results (output of compute_max_hits). Zero before the cap step runs (early-exit paths).
};

/// @brief Result of retrieve_manual_guidance().
struct GuidanceResult {
    std::string              text; ///< The guidance chunk text to inject; empty when nothing was injected.
    std::optional<QdrantPoint> source; ///< The Qdrant point for the injected guidance chunk; nullopt when nothing was injected.
    std::string reason; ///< Injection path: "route_forced_vector", "route_forced", "vector_search", or "" (not injected).
};

/// @brief Retrieve legislation sections as anchor context for the LLM prompt.
///
/// leg_store is nullable; pass nullptr when the jurisdiction has no separate
/// legislation collection (leg_collection is empty in CorpusConfig). Returns
/// an empty AnchorResult on error or null leg_store.
///
/// Pass `precomputed` to skip the internal build_route_decision call (saves
/// one AC scan per request when the caller already computed the decision).
/// Pass `precomputed_vec` to skip the internal embed() call (saves one embed
/// RTT when the caller already embedded the same question for corpus retrieval).
drogon::Task<AnchorResult> retrieve_anchor(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    VectorStore* leg_store,
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed     = nullptr,
    const std::vector<float>* precomputed_vec = nullptr);

/// @brief Supplementary case retrieval for matched routes that carry case_synthetic_query.
///
/// Extends context_texts and sources in-place and deduplicates by QdrantPoint.id.
/// No-op when no matched route carries a case_synthetic_query field.
/// `precomputed` has the same semantics as in retrieve_anchor().
drogon::Task<> augment_case_retrieval(
    std::string question,
    std::string retrieval_question,
    RAGPipeline& pipeline,
    const RouteTable& table,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources,
    const RouteDecision* precomputed = nullptr);

/// @brief Inject the top-1 authoritative MANUAL guidance chunk into the prompt.
///
/// Route-forced docs from guidance_sources are injected regardless of vector
/// score. Falls back to a vector threshold search (GUIDANCE_THRESHOLD) when
/// no route guidance is forced. Returns an empty GuidanceResult when nothing
/// meets the threshold. `precomputed` has the same semantics as in retrieve_anchor().
drogon::Task<GuidanceResult> retrieve_manual_guidance(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    const std::unordered_set<std::string>& existing_source_ids,
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed = nullptr);

/// @brief Second retrieval pass for low-confidence responses.
///
/// Runs a relaxed retrieve (top_k=8, min_score=0.65, min_chunks=1) against
/// both original and rewritten questions, merges with existing sources
/// in-place, re-sorts by score, and caps the combined set at 6.
/// Python parity: core/anchor.py:_refine_retrieve.
drogon::Task<> refine_retrieve(
    std::string original_question,
    std::string rewritten_question,
    RAGPipeline& pipeline,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources);

} // namespace astraea
