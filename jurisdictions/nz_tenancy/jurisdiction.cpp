#include "nz_tenancy/jurisdiction.hpp"
#include "nz_tenancy/prompt.hpp"
#include "nz_tenancy/routes.hpp"

namespace astraea::nz_tenancy {

NZTenancyJurisdiction::NZTenancyJurisdiction()
    : _name("nz-tenancy")
    , _corpus{
        .qdrant_collection = "nztt_moj",
        .leg_collection    = "nz_legal_v2",
        .courts            = {"Tenancy Tribunal"},
        .pg_database       = std::string{"nz_legal"},
      }
    , _legislation{
        .acts = {
            {"RTA", "https://www.legislation.govt.nz/act/public/1986/120/en/latest/"},
        },
        .cache_ttl_seconds = 3600,
      }
    , _leg_sources{
        LegislationSource{
            .act_id        = "RTA",
            .court_name    = "Residential Tenancies Act 1986",
            .default_top_k = 6,
            .boost_top_k   = 10,
        },
        LegislationSource{
            .act_id        = "HHS2019",
            .court_name    = "Residential Tenancies (Healthy Homes Standards) Regulations 2019",
            .default_top_k = 4,
            .boost_top_k   = 8,
        },
      }
{}

const std::string& NZTenancyJurisdiction::name() const {
    return _name;
}

std::string NZTenancyJurisdiction::description() const {
    return "Free NZ residential tenancy law Q&A - tenancy.localrun.ai";
}

const CorpusConfig& NZTenancyJurisdiction::corpus() const {
    return _corpus;
}

const std::string& NZTenancyJurisdiction::system_prompt() const {
    return SYSTEM_PROMPT;
}

std::span<const StatuteRoute> NZTenancyJurisdiction::routes() const {
    return get_routes();
}

std::optional<LegislationConfig> NZTenancyJurisdiction::legislation() const {
    return _legislation;
}

const std::vector<LegislationSource>& NZTenancyJurisdiction::leg_sources() const {
    return _leg_sources;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>&
NZTenancyJurisdiction::low_priority_sections() const {
    return get_low_priority_sections();
}

// Custom: defaults to "Tenancy Tribunal" rather than "Unknown" when the
// payload omits court_name. Matches Python jurisdiction.py:format_source_label.
std::string NZTenancyJurisdiction::format_source_label(
    const std::unordered_map<std::string, std::string>& source_payload) const
{
    auto it_court = source_payload.find("court_name");
    auto it_date  = source_payload.find("date");
    const std::string court = (it_court != source_payload.end())
                            ? it_court->second
                            : "Tenancy Tribunal";
    const std::string date  = (it_date != source_payload.end())
                            ? it_date->second
                            : "";
    return date.empty() ? court : (court + " - " + date);
}

} // namespace astraea::nz_tenancy
