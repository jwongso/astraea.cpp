#include "astraea/route_table.hpp"
#include "astraea/routing.hpp"
#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace astraea {

// ---------------------------------------------------------------------------
// normalize_query
// ---------------------------------------------------------------------------
// Both Python and C++ apply the same transliteration table (U+2018/2019/201C/201D
// -> ASCII quote, U+2012-2015 -> space, U+002D -> space). Python uses
// str.maketrans + str.lower; C++ does it in one byte-level pass with branchless
// ASCII lowercase. For ASCII input (all current route terms) the results are
// identical. The only latent divergence is non-ASCII casing (finding.md §1.5).

std::string normalize_query(std::string_view text) {
    std::string out;
    out.resize(text.size()); // output is never longer than input
    std::size_t w = 0;
    bool in_space = true; // suppress leading whitespace

    auto emit = [&](char c) {
        if (c == ' ') {
            if (!in_space) { out[w++] = ' '; in_space = true; }
        } else {
            out[w++] = c;
            in_space = false;
        }
    };

    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ) {
        const auto b = static_cast<unsigned char>(text[i]);

        if (b < 0x80) {
            if (b == 0x2D || b == ' ' || b == '\t' || b == '\n' || b == '\r')
                emit(' ');
            else
                emit(static_cast<char>(b | (static_cast<unsigned>(b - 'A') < 26u ? 0x20 : 0)));
            ++i;
            continue;
        }

        // E2 80 XX: smart quotes and dashes
        if (b == 0xE2 && i + 2 < n
                && static_cast<unsigned char>(text[i + 1]) == 0x80) {
            const auto b2 = static_cast<unsigned char>(text[i + 2]);
            switch (b2) {
                case 0x98: case 0x99: emit('\''); i += 3; continue; // U+2018/2019 -> '
                case 0x9C: case 0x9D: emit('"');  i += 3; continue; // U+201C/201D -> "
                case 0x92: case 0x93:
                case 0x94: case 0x95: emit(' ');  i += 3; continue; // U+2012-2015 -> space
                default: break;
            }
        }

        // Pass through other multi-byte bytes one at a time
        out[w++] = static_cast<char>(b);
        in_space = false;
        ++i;
    }
    if (w > 0 && out[w - 1] == ' ') --w; // strip trailing space
    out.resize(w);
    return out;
}

// ---------------------------------------------------------------------------
// Internal helpers (used for near-miss and allow_section only)
// ---------------------------------------------------------------------------

[[nodiscard]] static bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] static bool any_in(std::string_view q, const std::vector<std::string>& terms) {
    for (const auto& t : terms)
        if (contains(q, t)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// build_route_decision
// ---------------------------------------------------------------------------

// Body of build_route_decision, parameterised on the pre-built AC. Both public
// overloads delegate here. Keeping this private allows the two thin wrappers to
// share every line of aggregation logic.
static RouteDecision build_route_decision_impl(
    std::string_view original,
    std::string_view rewritten,
    std::span<const StatuteRoute> routes,
    const AhoCorasick& ac)
{
    std::string combined;
    combined.reserve(original.size() + 1 + rewritten.size());
    combined.append(original);
    combined.push_back(' ');
    combined.append(rewritten);
    const std::string q = normalize_query(combined);

    const auto hits = ac.search(q);

    // Per-route match summary derived from AC hits
    struct RM {
        bool has_exclude = false;
        bool has_precise = false;
        bool has_broad   = false;
        bool has_ctx     = false;
        bool has_legacy  = false;
        std::unordered_set<std::string_view> all_hit; // include_all terms seen
    };
    std::vector<RM> rm(routes.size());
    for (const auto& h : hits) {
        auto& m = rm[h.route_idx];
        switch (h.field) {
            case AcField::Exclude:  m.has_exclude = true;        break;
            case AcField::Precise:  m.has_precise = true;        break;
            case AcField::Broad:    m.has_broad   = true;        break;
            case AcField::Context:  m.has_ctx     = true;        break;
            case AcField::Legacy:   m.has_legacy  = true;        break;
            case AcField::All:      m.all_hit.insert(h.term);   break;
        }
    }

    // Match routes
    std::vector<int> matched_idx;
    std::vector<std::string> match_paths;  // parallel to matched_idx
    for (int i = 0; i < static_cast<int>(routes.size()); ++i) {
        const auto& r  = routes[i];
        const auto& m  = rm[i];
        if (m.has_exclude) continue;
        bool triggered = false;
        std::string path;
        if (!r.include_any_precise.empty() || !r.include_any_broad.empty()) {
            if (m.has_precise)             { triggered = true; path = "precise"; }
            else if (m.has_broad && m.has_ctx) { triggered = true; path = "broad+context"; }
        } else {
            if (m.has_legacy)              { triggered = true; path = "legacy"; }
        }
        if (!triggered) continue;
        // include_all: every term must have been seen
        if (!r.include_all.empty()) {
            bool all_ok = true;
            for (const auto& t : r.include_all) {
                if (!m.all_hit.count(std::string_view(t))) { all_ok = false; break; }
            }
            if (!all_ok) continue;
        }
        matched_idx.push_back(i);
        match_paths.push_back(std::move(path));
    }

    // Build matched pointer list (for downstream code that uses *r)
    std::vector<const StatuteRoute*> matched;
    matched.reserve(matched_idx.size());
    for (int i : matched_idx) matched.push_back(&routes[i]);

    // Forced sections (union, order preserved)
    std::vector<std::string> forced;
    std::unordered_set<std::string> seen_sections;
    for (const auto* r : matched)
        for (const auto& s : r->forced_sections)
            if (seen_sections.insert(s).second) forced.push_back(s);

    // Boosted act IDs from forced sections
    std::unordered_set<std::string> boosted;
    for (const auto& s : forced) {
        const auto slash  = s.find('/');
        if (slash == std::string::npos || slash + 1 >= s.size()) continue;
        const auto slash2 = s.find('/', slash + 1);
        boosted.insert(s.substr(slash + 1,
            slash2 == std::string::npos ? std::string::npos : slash2 - slash - 1));
    }

    // Allow-list: dominant route only (highest priority among routes defining one)
    std::vector<const StatuteRoute*> allow_candidates;
    for (const auto* r : matched)
        if (!r->leg_allow_list.empty()) allow_candidates.push_back(r);

    const StatuteRoute* dominant_ptr = nullptr;
    std::vector<std::string> leg_allow_list;

    if (!allow_candidates.empty()) {
        dominant_ptr = *std::max_element(allow_candidates.begin(), allow_candidates.end(),
            [](const StatuteRoute* a, const StatuteRoute* b) { return a->priority < b->priority; });
        leg_allow_list = dominant_ptr->leg_allow_list;
    } else if (!matched.empty()) {
        dominant_ptr = *std::max_element(matched.begin(), matched.end(),
            [](const StatuteRoute* a, const StatuteRoute* b) { return a->priority < b->priority; });
    }

    // Synthetic queries (deduplicated, order preserved)
    std::vector<std::string> leg_synths, case_synths;
    std::unordered_set<std::string> seen_lsynth, seen_csynth;
    for (const auto* r : matched) {
        if (!r->synthetic_query.empty() && seen_lsynth.insert(r->synthetic_query).second)
            leg_synths.push_back(r->synthetic_query);
        if (!r->case_synthetic_query.empty() && seen_csynth.insert(r->case_synthetic_query).second)
            case_synths.push_back(r->case_synthetic_query);
    }

    // Trigger terms + paths - collected from AC hits for matched routes
    std::unordered_set<int> matched_set(matched_idx.begin(), matched_idx.end());
    std::unordered_set<std::string> trigger_term_set;
    std::vector<TriggerPath> trigger_paths;

    // Build a route_idx -> path_string map for O(1) lookup below
    std::unordered_map<int, std::string_view> route_path;
    for (std::size_t k = 0; k < matched_idx.size(); ++k) {
        trigger_paths.push_back({routes[matched_idx[k]].intent, match_paths[k]});
        route_path[matched_idx[k]] = match_paths[k];
    }

    for (const auto& h : hits) {
        if (!matched_set.count(h.route_idx)) continue;
        const auto& path = route_path[h.route_idx];
        bool include = false;
        if (path == "precise"       && h.field == AcField::Precise)  include = true;
        if (path == "broad+context" && (h.field == AcField::Broad || h.field == AcField::Context))
            include = true;
        if (path == "legacy"        && h.field == AcField::Legacy)   include = true;
        if (include) trigger_term_set.insert(std::string(h.term));
    }
    std::vector<std::string> trigger_terms(trigger_term_set.begin(), trigger_term_set.end());
    std::sort(trigger_terms.begin(), trigger_terms.end());

    // Near-miss routes (broad hit but no context match, and not already matched)
    std::vector<NearMiss> near_misses;
    for (int i = 0; i < static_cast<int>(routes.size()); ++i) {
        if (matched_set.count(i)) continue;
        const auto& r = routes[i];
        const auto& m = rm[i];
        if (!r.include_any_broad.empty() && m.has_broad && !m.has_ctx) {
            NearMiss nm;
            nm.intent = r.intent;
            for (const auto& h : hits)
                if (h.route_idx == i && h.field == AcField::Broad)
                    nm.broad_matched.push_back(std::string(h.term));
            near_misses.push_back(std::move(nm));
        }
    }

    // Dominance audit
    std::string dominant_route, dominance_reason;
    std::vector<IgnoredRoute> ignored;
    if (!matched.empty() && dominant_ptr) {
        dominant_route = dominant_ptr->intent;
        if (!allow_candidates.empty()) {
            dominance_reason = dominant_ptr->priority > 0
                ? std::format("has leg_allow_list, priority {}", dominant_ptr->priority)
                : "has leg_allow_list";
            for (const auto* r : matched) {
                if (r == dominant_ptr) continue;
                std::string why = r->leg_allow_list.empty()
                    ? "no allow-list; forced sections still merged"
                    : std::format("lower priority ({} < {}); allow-list not used, forced sections still merged",
                                  r->priority, dominant_ptr->priority);
                ignored.push_back({r->intent, std::move(why)});
            }
        } else {
            dominance_reason = std::format("highest priority ({}); no matched routes define leg_allow_list",
                                           dominant_ptr->priority);
            for (const auto* r : matched) {
                if (r == dominant_ptr) continue;
                ignored.push_back({r->intent, "lower priority; forced sections still merged"});
            }
        }
    }

    // Assemble result
    RouteDecision d;
    d.triggered = !matched.empty();
    for (const auto* r : matched) d.matched_intents.push_back(r->intent);
    d.trigger_terms    = std::move(trigger_terms);
    d.trigger_paths    = std::move(trigger_paths);
    d.forced_sections  = std::move(forced);
    d.leg_allow_list   = std::move(leg_allow_list);
    d.boosted_act_ids  = std::move(boosted);
    d.leg_synthetic_queries  = std::move(leg_synths);
    d.case_synthetic_queries = std::move(case_synths);
    d.dominant_route   = std::move(dominant_route);
    d.dominance_reason = std::move(dominance_reason);
    d.ignored_routes   = std::move(ignored);
    d.near_miss_routes = std::move(near_misses);
    return d;
}

// Legacy entry point: builds an AC per call. Kept so existing callers and
// tests continue to compile. Prefer the RouteTable overload on every request
// hot path — RouteTable caches the AC across calls.
RouteDecision build_route_decision(
    std::string_view original,
    std::string_view rewritten,
    std::span<const StatuteRoute> routes)
{
    const AhoCorasick ac(routes);
    return build_route_decision_impl(original, rewritten, routes, ac);
}

// New entry point: reuses the pre-built AC inside the RouteTable.
RouteDecision build_route_decision(
    std::string_view  original,
    std::string_view  rewritten,
    const RouteTable& table)
{
    return build_route_decision_impl(original, rewritten, table.routes(), table.ac());
}

// ---------------------------------------------------------------------------
// allow_section
// ---------------------------------------------------------------------------

bool allow_section(
    std::string_view case_id,
    std::string_view combined_query,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& low_priority_sections)
{
    for (const auto& [id, terms] : low_priority_sections) {
        if (id == case_id) {
            return any_in(combined_query, terms);
        }
    }
    return true;
}

} // namespace astraea
