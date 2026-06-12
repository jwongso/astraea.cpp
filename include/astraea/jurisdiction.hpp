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

namespace detail {
// Shared helper for virtual methods that return a const ref to an empty
// default. Magic statics are thread-safe under C++11 and later.
template <typename T>
const T& empty_default() {
    static const T v{};
    return v;
}
} // namespace detail

// Pointers to the data stores for this jurisdiction.
struct CorpusConfig {
    std::string qdrant_collection;
    std::string leg_collection;               // empty = no separate legislation collection
    std::vector<std::string> courts;          // empty = all courts
    std::optional<std::string> pg_database;   // nullopt = no SQL retrieval path (v1)
};

// Live legislation extraction settings.
// Note: acts is std::map (sorted), not insertion-ordered like Python dict.
// Currently only used as keyed lookup (acts[act_id]), so order does not matter.
// If a future consumer needs iteration in definition order, switch to
// std::vector<std::pair<std::string, std::string>>.
struct LegislationConfig {
    std::map<std::string, std::string> acts; // act_id -> URL
    int cache_ttl_seconds = 3600;
};

// Per-Act quota for federated legislation retrieval.
// When leg_sources() is non-empty, anchor runs one Qdrant search per source
// in parallel instead of a single global search.
struct LegislationSource {
    std::string act_id;
    std::string court_name; // value of court_name payload field in Qdrant
    int default_top_k = 4;  // matches Python default
    int boost_top_k   = 8;  // matches Python default
};

// Confidence thresholds and messages. Defaults match Python reference values.
struct ConfidenceConfig {
    float high_score   = 0.82f;
    int   high_n       = 4;
    float medium_score = 0.77f;
    int   medium_n     = 2;
    std::map<std::string, std::string> messages = {
        {"high",   "Found {n} directly relevant decisions."},
        {"medium", "Found {n} relevant decisions - review the cited sources carefully."},
        {"low",    "Found only {n} loosely related decisions - verify independently before acting."},
        {"none",   "No relevant decisions found."},
    };
};

// Tier 1 retrieval smoke test fixture.
struct SmokeFixture {
    std::string question;
    std::vector<std::string> expected_sections;          // MUST appear in retrieval
    std::vector<std::string> forbidden_sections;         // MUST NOT appear
    std::string description;
    int min_sources = 0;                                 // assert >= this many case sources returned
    std::vector<std::string> expected_guidance_sources;  // MANUAL case_ids that MUST be injected
};

// Pure routing test fixture - no Qdrant, no HTTP, no LLM.
struct RouteFixture {
    std::string question;
    std::vector<std::string> expected_routes;   // MUST fire
    std::vector<std::string> forbidden_routes;  // MUST NOT fire
    std::string description;
    std::string rewritten; // empty = use question as both original and rewritten
};

// Abstract base - one subclass per jurisdiction.
class JurisdictionBase {
public:
    virtual ~JurisdictionBase() = default;

    // Required
    virtual const std::string&            name()          const = 0;
    virtual const CorpusConfig&           corpus()        const = 0;
    virtual const std::string&            system_prompt() const = 0;
    virtual std::span<const StatuteRoute> routes()        const = 0;

    // Optional - override as needed

    // Returns "<name> legal research tool" by default, matching Python.
    virtual std::string description() const {
        return name() + " legal research tool";
    }

    virtual std::optional<LegislationConfig> legislation() const { return std::nullopt; }

    virtual int   max_question_chars()  const { return 1200; }
    virtual float leg_ce_min_score()    const { return 0.15f; }
    virtual bool  log_route_decisions() const { return false; }

    virtual const ConfidenceConfig& confidence_config() const {
        return detail::empty_default<ConfidenceConfig>();
    }

    virtual const std::vector<LegislationSource>& leg_sources() const {
        return detail::empty_default<std::vector<LegislationSource>>();
    }

    virtual const std::vector<std::pair<std::string, std::vector<std::string>>>&
    low_priority_sections() const {
        return detail::empty_default<
            std::vector<std::pair<std::string, std::vector<std::string>>>>();
    }

    virtual const std::vector<std::string>& forbidden_topics() const {
        return detail::empty_default<std::vector<std::string>>();
    }

    // Transform question after sanitization, before retrieval.
    virtual std::string preprocess_question(
        std::string_view question,
        std::optional<std::string> /*address*/ = std::nullopt) const {
        return std::string(question);
    }

    // nullopt = use core default rewrite prompt; empty string = skip rewrite.
    virtual std::optional<std::string> rewrite_prompt() const { return std::nullopt; }

    // Render a source payload as a display label.
    // source_payload maps field names (court_name, date, ...) to string values.
    virtual std::string format_source_label(
        const std::unordered_map<std::string, std::string>& source_payload) const {
        auto it_court = source_payload.find("court_name");
        auto it_date  = source_payload.find("date");
        const std::string court = (it_court != source_payload.end()) ? it_court->second : "Unknown";
        const std::string date  = (it_date  != source_payload.end()) ? it_date->second  : "";
        return date.empty() ? court : (court + " - " + date);
    }

    // Return the relevant excerpt from live Act text, or nullopt to use the
    // core heading-aware extractor. Override for unusual legislation formatting.
    virtual std::optional<std::string> extract_section(
        std::string_view /*act_id*/,
        std::string_view /*section*/,
        std::string_view /*full_text*/) const {
        return std::nullopt;
    }

    virtual const std::vector<RouteFixture>& route_fixtures() const {
        return detail::empty_default<std::vector<RouteFixture>>();
    }

    virtual const std::vector<SmokeFixture>& smoke_fixtures() const {
        return detail::empty_default<std::vector<SmokeFixture>>();
    }
};

} // namespace astraea
