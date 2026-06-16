#include "building_consents/jurisdiction.hpp"
#include "building_consents/prompt.hpp"
#include "building_consents/routes.hpp"

namespace astraea::nz_building {

NZBuildingJurisdiction::NZBuildingJurisdiction()
    : _name("nz-building")
    , _corpus{
        .qdrant_collection = "nz_building_leg",
        .leg_collection    = "nz_building_leg",
        .courts            = {},
      }
    , _confidence{
        .high_score   = 0.75f,
        .high_n       = 3,
        .medium_score = 0.68f,
        .medium_n     = 1,
        .messages = {
            {"high",   "Found {n} directly relevant legislation sections."},
            {"medium", "Found {n} relevant legislation sections - review carefully before acting."},
            {"low",    "Found only {n} loosely related legislation sections - verify with your council."},
            {"none",   "No relevant legislation found."},
        },
    }
{}

const std::string& NZBuildingJurisdiction::name() const { return _name; }

std::string NZBuildingJurisdiction::description() const {
    return "Free NZ building consents and zone lookup - buildingconsents.localrun.ai";
}

const CorpusConfig& NZBuildingJurisdiction::corpus() const { return _corpus; }

const std::string& NZBuildingJurisdiction::system_prompt() const { return SYSTEM_PROMPT; }

std::span<const StatuteRoute> NZBuildingJurisdiction::routes() const { return get_routes(); }

const ConfidenceConfig& NZBuildingJurisdiction::confidence_config() const { return _confidence; }

std::optional<std::string> NZBuildingJurisdiction::rewrite_prompt() const {
    return REWRITE_PROMPT;
}

} // namespace astraea::nz_building
