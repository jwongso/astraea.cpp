#pragma once
#include "astraea/routing.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace astraea {

struct CorpusConfig {
    std::string qdrant_collection;
    std::string leg_collection;
    std::vector<std::string> courts; // empty = all courts
};

struct LegislationSource {
    std::string act_id;
    std::string court_name; // value of court_name payload field in Qdrant
    int default_top_k = 3;
    int boost_top_k   = 6;
};

struct ConfidenceConfig {
    float high_score   = 0.85f;
    int   high_n       = 2;
    float medium_score = 0.75f;
    int   medium_n     = 1;
    std::map<std::string, std::string> messages = {
        {"high",   "Found {n} directly relevant decisions."},
        {"medium", "Found {n} related decisions."},
        {"low",    "Limited relevant decisions found."},
        {"none",   "No relevant sources found."},
    };
};

struct RouteFixture {
    std::string question;
    std::vector<std::string> expected_routes;
    std::vector<std::string> forbidden_routes;
    std::string description;
};

struct SmokeFixture {
    std::string question;
    std::vector<std::string> expected_sections;
    std::string description;
};

// Abstract base - one subclass per jurisdiction.
class JurisdictionBase {
public:
    virtual ~JurisdictionBase() = default;

    virtual const std::string&     name()          const = 0;
    virtual const std::string&     description()   const = 0;
    virtual const CorpusConfig&    corpus()        const = 0;
    virtual const std::string&     system_prompt() const = 0;
    virtual std::span<const StatuteRoute> routes() const = 0;

    virtual int    max_question_chars() const { return 1200; }
    virtual float  leg_ce_min_score()   const { return 0.15f; }
    virtual bool   log_route_decisions()const { return false; }
    virtual const ConfidenceConfig& confidence_config() const {
        static const ConfidenceConfig def;
        return def;
    }
    virtual const std::vector<LegislationSource>& leg_sources() const {
        static const std::vector<LegislationSource> empty;
        return empty;
    }
    virtual const std::vector<std::pair<std::string,std::vector<std::string>>>&
        low_priority_sections() const {
        static const std::vector<std::pair<std::string,std::vector<std::string>>> empty;
        return empty;
    }
    // Optional jurisdiction-specific question preprocessing
    virtual std::string preprocess_question(
        std::string_view question,
        std::optional<std::string> /*address*/ = std::nullopt) const {
        return std::string(question);
    }
    // Optional rewrite prompt override (empty string = skip rewrite)
    virtual std::optional<std::string> rewrite_prompt() const { return std::nullopt; }

    virtual const std::vector<RouteFixture>& route_fixtures() const {
        static const std::vector<RouteFixture> empty;
        return empty;
    }
    virtual const std::vector<SmokeFixture>& smoke_fixtures() const {
        static const std::vector<SmokeFixture> empty;
        return empty;
    }
};

} // namespace astraea
