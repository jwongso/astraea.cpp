#pragma once
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace astraea {

// Direct port of Python StatuteRoute dataclass.
// All string fields are lowercase at definition time - no runtime lowercasing needed.
struct StatuteRoute {
    std::string intent;
    // Two-tier matching (replaces include_any when either is non-empty)
    std::vector<std::string> include_any_precise;
    std::vector<std::string> include_any_broad;
    std::vector<std::string> require_context_any;
    // Legacy flat-list matching (used when precise/broad are empty)
    std::vector<std::string> include_any;
    std::vector<std::string> include_all;
    std::vector<std::string> exclude_any;
    std::vector<std::string> forced_sections;
    std::vector<std::string> leg_allow_list;
    std::vector<std::string> guidance_sources;
    std::string synthetic_query;
    std::string case_synthetic_query;
    int priority = 0;
    std::string notes;
};

struct TriggerPath {
    std::string intent;
    std::string path; // "precise" | "broad+context" | "legacy"
};

struct NearMiss {
    std::string intent;
    std::vector<std::string> broad_matched;
};

struct IgnoredRoute {
    std::string intent;
    std::string reason;
};

// Fully computed routing decision - direct port of Python RouteDecision.
struct RouteDecision {
    bool triggered = false;
    std::vector<std::string> matched_intents;
    std::vector<std::string> trigger_terms;
    std::vector<TriggerPath> trigger_paths;
    std::vector<std::string> forced_sections;
    std::vector<std::string> leg_allow_list;
    std::unordered_set<std::string> boosted_act_ids;
    std::vector<std::string> leg_synthetic_queries;
    std::vector<std::string> case_synthetic_queries;
    std::string dominant_route;
    std::string dominance_reason;
    std::vector<IgnoredRoute> ignored_routes;
    std::vector<NearMiss> near_miss_routes;
};

// Normalise user input for trigger matching:
// lowercase, curly quotes/dashes -> ASCII equivalents, collapse whitespace.
std::string normalize_query(std::string_view text);

// Single public entry point. Mirrors Python build_route_decision exactly.
RouteDecision build_route_decision(
    std::string_view original,
    std::string_view rewritten,
    std::span<const StatuteRoute> routes);

// Returns false to suppress low-priority sections not relevant for this query.
bool allow_section(
    std::string_view case_id,
    std::string_view combined_query,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& low_priority_sections);

} // namespace astraea
