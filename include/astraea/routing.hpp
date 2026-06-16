#pragma once
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace astraea {

/// @brief One keyword-routing rule binding a legal intent to trigger terms and retrieval hints.
///
/// Direct port of the Python StatuteRoute dataclass. All string fields are
/// stored in lowercase at definition time so no runtime lowercasing is needed
/// during the hot matching path.
///
/// Two-tier matching: when include_any_precise or include_any_broad is non-empty
/// they replace include_any. Precise terms trigger on their own; broad terms
/// require at least one require_context_any term to also be present.
struct StatuteRoute {
    std::string intent; ///< Short identifier for this route, e.g. "bond_return"; appears in debug output.
    /// Two-tier matching (replaces include_any when either is non-empty).
    std::vector<std::string> include_any_precise; ///< Any one of these terms triggers the route on its own.
    std::vector<std::string> include_any_broad; ///< Any one of these triggers only when a require_context_any term also matches.
    std::vector<std::string> require_context_any; ///< Context guard for broad terms; at least one must match.
    /// Legacy flat-list matching (used when precise/broad are empty).
    std::vector<std::string> include_any; ///< Legacy trigger; any one of these fires the route (used when precise/broad lists are empty).
    std::vector<std::string> include_all; ///< All of these must match (AND condition); checked after include_any logic passes.
    std::vector<std::string> exclude_any; ///< If any of these match the route is suppressed regardless of include hits.
    std::vector<std::string> forced_sections; ///< Qdrant point IDs injected into retrieval unconditionally when this route fires.
    std::vector<std::string> leg_allow_list; ///< Act IDs allowed through the CE gate even below the score threshold.
    std::vector<std::string> guidance_sources; ///< MANUAL collection point IDs eligible for guidance injection when this route fires.
    std::string synthetic_query; ///< Pre-embedded synthetic query for legislation retrieval; cached at startup.
    std::string case_synthetic_query; ///< Pre-embedded synthetic query for supplementary case retrieval.
    int priority = 0; ///< Higher-priority routes win dominance when multiple fire; ties broken by definition order.
    std::string notes; ///< Free-text explanation for humans; not used at runtime.
};

/// @brief Which matching tier fired for a given route intent.
struct TriggerPath {
    std::string intent; ///< The route intent that triggered.
    std::string path; ///< One of "precise", "broad+context", or "legacy".
};

/// @brief A route that matched its broad terms but lacked required context, recorded for debug output.
struct NearMiss {
    std::string intent; ///< The route intent that came close.
    std::vector<std::string> broad_matched; ///< The broad terms that matched even though context was absent.
};

/// @brief A route suppressed by an exclude_any hit, recorded for debug output.
struct IgnoredRoute {
    std::string intent; ///< The route intent that was suppressed.
    std::string reason; ///< Human-readable explanation of why it was ignored.
};

/// @brief Fully computed routing decision for one (original, rewritten) question pair.
///
/// Direct port of Python RouteDecision. Produced by build_route_decision() and
/// threaded through the entire retrieval pipeline so each stage can read it
/// without re-running the AC scan.
struct RouteDecision {
    bool triggered = false; ///< True if at least one route fired.
    std::vector<std::string> matched_intents; ///< Intent names of all fired routes, in discovery order.
    std::vector<std::string> trigger_terms; ///< The specific terms that caused each matched route to fire.
    std::vector<TriggerPath> trigger_paths; ///< Per-intent tier that caused the match.
    std::vector<std::string> forced_sections; ///< Union of forced_sections from all fired routes.
    std::vector<std::string> leg_allow_list; ///< Union of leg_allow_list from all fired routes.
    std::unordered_set<std::string> boosted_act_ids; ///< Act IDs that receive the boost_top_k budget in federated legislation retrieval.
    std::vector<std::string> leg_synthetic_queries; ///< Synthetic queries from fired routes for legislation retrieval.
    std::vector<std::string> case_synthetic_queries; ///< Synthetic queries from fired routes for supplementary case retrieval.
    std::string dominant_route; ///< Intent of the highest-priority fired route; empty when no route fired.
    std::string dominance_reason; ///< Human-readable explanation of why dominant_route won.
    std::vector<IgnoredRoute> ignored_routes; ///< Routes suppressed by exclude_any, for debug output.
    std::vector<NearMiss> near_miss_routes; ///< Routes that matched broad terms but lacked required context.
};

/// @brief Normalise user input for trigger matching.
///
/// Lowercases the text and converts curly quotes, em dashes, and similar
/// Unicode punctuation to their ASCII equivalents, then collapses whitespace.
/// @return The normalised ASCII string ready for Aho-Corasick search.
std::string normalize_query(std::string_view text);

/// @brief Compute the full routing decision for a question pair.
///
/// Mirrors Python build_route_decision() exactly. Builds a fresh AhoCorasick
/// automaton from `routes` on every call; prefer the RouteTable overload on
/// per-request code paths to reuse the pre-built automaton.
RouteDecision build_route_decision(
    std::string_view original,
    std::string_view rewritten,
    std::span<const StatuteRoute> routes);

/// @brief Filter function for low-priority legislation sections.
///
/// Returns false when case_id belongs to a low-priority section group AND
/// none of the group's required terms appear in combined_query, suppressing
/// that section from the final context. Returns true (allow) otherwise.
bool allow_section(
    std::string_view case_id,
    std::string_view combined_query,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& low_priority_sections);

} // namespace astraea
