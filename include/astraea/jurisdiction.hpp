#pragma once
#include "astraea/routing.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace astraea {

// Framework-default rewrite prompt. Used when a jurisdiction's rewrite_prompt()
// returns nullopt. Verbatim port of core/api.py:_REWRITE_SYSTEM_DEFAULT.
inline const std::string DEFAULT_REWRITE_PROMPT =
    "Rewrite the following as a concise formal legal question optimised for "
    "retrieving relevant case decisions. Focus on the underlying legal dispute, "
    "facts, and claims (e.g. what damage is alleged, what the landlord or tenant "
    "is claiming, what the legal issue is). If the question includes procedural "
    "sub-questions about the tribunal process (wait times, hearing format, "
    "evidence deadlines), ignore those entirely - they are not useful for case "
    "retrieval. Output only the rewritten question, no explanation, no preamble.";

namespace detail {
// Shared helper for virtual methods that return a const ref to an empty
// default. Magic statics are thread-safe under C++11 and later.
template <typename T>
const T& empty_default() {
    static const T v{};
    return v;
}
} // namespace detail

/// @brief Qdrant collection names and optional PostgreSQL database for one jurisdiction.
///
/// Passed to VectorStore and RAGPipeline constructors. leg_collection is the
/// dedicated legislation collection; when empty, legislation chunks share the
/// main qdrant_collection and are distinguished by a court_name payload field.
struct CorpusConfig {
    std::string qdrant_collection; ///< Name of the primary Qdrant collection for case-law chunks.
    std::string leg_collection; ///< Dedicated legislation collection; empty means no separate collection.
    std::vector<std::string> courts; ///< court_name values to filter on; empty means accept all courts.
    std::optional<std::string> pg_database; ///< PostgreSQL database name for SQL retrieval; nullopt disables the SQL path (v1 not yet wired).
};

/// @brief Settings for live legislation text extraction from legislation.govt.nz.
///
/// Note: acts is std::map (sorted), not insertion-ordered like Python dict.
/// Currently only used as keyed lookup (acts[act_id]), so order does not matter.
/// If a future consumer needs iteration in definition order, switch to
/// std::vector<std::pair<std::string, std::string>>.
struct LegislationConfig {
    std::map<std::string, std::string> acts; ///< Map of act_id to canonical URL for live fetch.
    int cache_ttl_seconds = 3600; ///< How long fetched Act text is cached in memory before re-fetching.
};

/// @brief Per-Act quota configuration for federated legislation retrieval.
///
/// When leg_sources() is non-empty, anchor runs one Qdrant search per source
/// in parallel instead of a single global search, allowing different Acts to
/// receive different top_k budgets and to be individually boosted by routing.
struct LegislationSource {
    std::string act_id; ///< Matches the key in LegislationConfig::acts and the Qdrant payload.
    std::string court_name; ///< Value of the court_name payload field in Qdrant used to filter to this Act.
    int default_top_k = 4; ///< Chunks retrieved when no route boost applies; matches Python default.
    int boost_top_k   = 8; ///< Chunks retrieved when this Act's act_id is in RouteDecision::boosted_act_ids; matches Python default.
};

/// @brief Confidence score thresholds and display messages for the confidence SSE event.
///
/// Defaults match the Python reference values in core/api.py. The {n} placeholder
/// in message templates is substituted with the actual chunk count at runtime.
struct ConfidenceConfig {
    float high_score   = 0.82f; ///< Top source score required for "high" confidence; must be >= medium_score.
    int   high_n       = 4; ///< Minimum number of sources for "high" confidence to fire.
    float medium_score = 0.77f; ///< Top source score required for "medium" confidence.
    int   medium_n     = 2; ///< Minimum number of sources for "medium" confidence to fire.
    std::map<std::string, std::string> messages = {
        {"high",   "Found {n} directly relevant decisions."},
        {"medium", "Found {n} relevant decisions - review the cited sources carefully."},
        {"low",    "Found only {n} loosely related decisions - verify independently before acting."},
        {"none",   "No relevant decisions found."},
    }; ///< Human-readable messages keyed by level ("high", "medium", "low", "none"); {n} is replaced with chunk count.
};

/// @brief Integration test fixture for the full retrieve path (requires live Qdrant).
///
/// Ported from Python commit 47ad14b. expected_sections must appear in retrieval
/// results, forbidden_sections must not appear, and min_sources sets a floor on
/// the case-source count. expected_guidance_sources validates that MANUAL
/// case_ids declared in a matched StatuteRoute::guidance_sources were injected.
struct SmokeFixture {
    std::string question; ///< The question sent through the full pipeline.
    std::vector<std::string> expected_sections; ///< Qdrant point IDs that MUST appear in retrieval results.
    std::vector<std::string> forbidden_sections; ///< Qdrant point IDs that MUST NOT appear in retrieval results.
    std::string description; ///< Human-readable description printed on test failure.
    int min_sources = 0; ///< Assert that at least this many case-law sources are returned.
    std::vector<std::string> expected_guidance_sources; ///< MANUAL case_ids that must be present in guidance injection output.
};

/// @brief Pure routing logic test fixture; no Qdrant, HTTP, or LLM calls.
///
/// Used by the offline routing test suite to verify route matching without
/// any network dependencies.
struct RouteFixture {
    std::string question; ///< The question sent to build_route_decision as `original`.
    std::vector<std::string> expected_routes; ///< Intent names that MUST appear in matched_intents.
    std::vector<std::string> forbidden_routes; ///< Intent names that MUST NOT appear in matched_intents.
    std::string description; ///< Human-readable description printed on test failure.
    std::string rewritten; ///< Optional rewritten form; empty means use question for both original and rewritten inputs.
};

/// @brief Plugin abstraction for jurisdiction-specific behaviour; one concrete subclass per jurisdiction.
///
/// Subclasses live under jurisdictions/ and are instantiated once at startup.
/// All virtual methods are const and thread-safe (they return references to
/// static or member data). The framework owns a const reference to the
/// concrete instance for the lifetime of the server.
///
/// Python parity: mirrors the JurisdictionBase protocol in core/jurisdiction.py.
class JurisdictionBase {
public:
    virtual ~JurisdictionBase() = default;

    /// @brief Short identifier used in Redis key prefixes and log lines.
    /// @return A stable lowercase ASCII name such as "nz_tenancy".
    virtual const std::string&            name()          const = 0;

    /// @brief Qdrant collection names and data-store config for this jurisdiction.
    virtual const CorpusConfig&           corpus()        const = 0;

    /// @brief LLM system prompt injected as the first message on every /ask call.
    virtual const std::string&            system_prompt() const = 0;

    /// @brief Ordered list of statute routes used to build the Aho-Corasick automaton.
    virtual std::span<const StatuteRoute> routes()        const = 0;

    /// @brief Human-readable description for UI display; defaults to "<name> legal research tool".
    virtual std::string description() const {
        return name() + " legal research tool";
    }

    /// @brief Live legislation fetch config; nullopt means no live-fetch path for this jurisdiction.
    virtual std::optional<LegislationConfig> legislation() const { return std::nullopt; }

    /// @brief Maximum allowed question length in characters; questions exceeding this are rejected with 400.
    virtual int   max_question_chars()  const { return 1200; }

    /// @brief Minimum cross-encoder score for a legislation chunk to survive the CE gate in retrieve_anchor.
    virtual float leg_ce_min_score()    const { return 0.15f; }

    /// @brief When true, route decision details are written to route_debug.jsonl; useful for tuning.
    virtual bool  log_route_decisions() const { return false; }

    /// @brief Confidence thresholds and messages; returns all-default values unless overridden.
    virtual const ConfidenceConfig& confidence_config() const {
        return detail::empty_default<ConfidenceConfig>();
    }

    /// @brief Per-Act retrieval quotas for federated legislation search; empty means single global search.
    virtual const std::vector<LegislationSource>& leg_sources() const {
        return detail::empty_default<std::vector<LegislationSource>>();
    }

    /// @brief Pairs of (case_id prefix, section list) for sections that are suppressed unless a route boosts them.
    virtual const std::vector<std::pair<std::string, std::vector<std::string>>>&
    low_priority_sections() const {
        return detail::empty_default<
            std::vector<std::pair<std::string, std::vector<std::string>>>>();
    }

    /// @brief Topic keywords that cause the pipeline to refuse the question with a 400 before any retrieval.
    virtual const std::vector<std::string>& forbidden_topics() const {
        return detail::empty_default<std::vector<std::string>>();
    }

    /// @brief Transform the sanitized question before embedding and retrieval.
    ///
    /// Called after sanitize_question() and before the rewrite step. The default
    /// implementation is a no-op. Override to prepend zone-context prefixes or
    /// other jurisdiction-specific enrichments. The `address` parameter is
    /// populated only when the request body includes an "address" field.
    /// @return The transformed question string; returning the original is valid.
    virtual std::string preprocess_question(
        std::string_view question,
        std::optional<std::string> /*address*/ = std::nullopt) const {
        return std::string(question);
    }

    /// @brief Override the LLM query-rewrite system prompt.
    ///
    /// @return nullopt to use DEFAULT_REWRITE_PROMPT; an empty string to skip
    ///         the rewrite step entirely; any other string to use as the system prompt.
    virtual std::optional<std::string> rewrite_prompt() const { return std::nullopt; }

    /// @brief Render a Qdrant source payload as a short human-readable label.
    ///
    /// Default format is "court_name - date" (or just "court_name" when date is absent).
    /// Override to apply jurisdiction-specific citation formatting.
    /// @param source_payload The full Qdrant payload map for one search result.
    /// @return A display string suitable for the UI citation chip.
    virtual std::string format_source_label(
        const std::unordered_map<std::string, std::string>& source_payload) const {
        auto it_court = source_payload.find("court_name");
        auto it_date  = source_payload.find("date");
        const std::string court = (it_court != source_payload.end()) ? it_court->second : "Unknown";
        const std::string date  = (it_date  != source_payload.end()) ? it_date->second  : "";
        return date.empty() ? court : (court + " - " + date);
    }

    /// @brief Extract a legislation section excerpt from full Act text.
    ///
    /// @return nullopt to use the core heading-aware extractor; a non-empty
    ///         string to use as the extracted excerpt directly. Override when
    ///         the Act has non-standard heading formatting the core extractor
    ///         cannot handle.
    virtual std::optional<std::string> extract_section(
        std::string_view /*act_id*/,
        std::string_view /*section*/,
        std::string_view /*full_text*/) const {
        return std::nullopt;
    }

    /// @brief Routing unit-test fixtures for this jurisdiction; empty by default.
    virtual const std::vector<RouteFixture>& route_fixtures() const {
        return detail::empty_default<std::vector<RouteFixture>>();
    }

    /// @brief Integration smoke-test fixtures for this jurisdiction; empty by default.
    virtual const std::vector<SmokeFixture>& smoke_fixtures() const {
        return detail::empty_default<std::vector<SmokeFixture>>();
    }
};

} // namespace astraea
