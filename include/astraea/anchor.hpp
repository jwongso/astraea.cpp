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

// Score threshold for injecting manual guidance. A guidance candidate must
// score at or above this value to be included. Promoted to the header so
// callers (e.g. context_debug event assembly in main.cpp) can reference the
// canonical value rather than duplicating a magic number.
inline constexpr float GUIDANCE_THRESHOLD = 0.75f;

// Result of retrieve_anchor.
struct AnchorResult {
    std::string              anchor_text;
    std::vector<QdrantPoint> leg_sources;
    // CE gate log omitted - will be emitted via spdlog when that wiring lands.
    double                   elapsed_ms = 0.0; // wall time of retrieve_anchor()
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
// table must be pre-built from jurisdiction.routes() and kept alive by the caller.
// precomputed: optional pre-built RouteDecision over the same (question,
// retrieval_question) inputs. When non-null, the internal build_route_decision
// call is skipped - saves one AC scan per request when the caller computes
// the decision once upfront and threads it to all helpers that need it.
drogon::Task<AnchorResult> retrieve_anchor(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    VectorStore* leg_store,
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed = nullptr);

// Supplementary case retrieval for matched routes with case_synthetic_query.
// Extends context_texts and sources in-place; deduplicates by QdrantPoint.id.
// No-op when no matched route carries a case_synthetic_query.
// precomputed: same semantics as retrieve_anchor's parameter of the same name.
drogon::Task<> augment_case_retrieval(
    std::string question,
    std::string retrieval_question,
    RAGPipeline& pipeline,
    const RouteTable& table,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources,
    const RouteDecision* precomputed = nullptr);

// Top-1 authoritative MANUAL guidance chunk as a parallel injection.
// Route-forced docs from matched routes are injected regardless of vector score.
// Falls back to a vector threshold search when no route guidance is forced.
// precomputed: same semantics as retrieve_anchor's parameter of the same name.
drogon::Task<GuidanceResult> retrieve_manual_guidance(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    const std::unordered_set<std::string>& existing_source_ids,
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed = nullptr);

// Second retrieval pass for low-confidence responses.
// Extends context_texts and sources in-place, re-sorts by score, caps at 6.
drogon::Task<> refine_retrieve(
    std::string original_question,
    std::string rewritten_question,
    RAGPipeline& pipeline,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources);

} // namespace astraea
