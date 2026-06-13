#pragma once
#include "astraea/jurisdiction.hpp"

namespace astraea::nz_tenancy {

// Concrete jurisdiction for NZ residential tenancy law.
//
// Port of jurisdictions/nz_tenancy/jurisdiction.py:NZTenancyJurisdiction.
// Owns no mutable state; safe to instantiate once at process startup and
// share by const reference across all request handlers.
class NZTenancyJurisdiction final : public JurisdictionBase {
public:
    NZTenancyJurisdiction();

    const std::string&            name()          const override;
    std::string                   description()   const override;
    const CorpusConfig&           corpus()        const override;
    const std::string&            system_prompt() const override;
    std::span<const StatuteRoute> routes()        const override;

    std::optional<LegislationConfig>              legislation()           const override;
    const std::vector<LegislationSource>&         leg_sources()           const override;
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
                                                  low_priority_sections() const override;

    float leg_ce_min_score()    const override { return 0.50f; }
    bool  log_route_decisions() const override { return true; }
    int   max_question_chars()  const override { return 1200; }

    std::string format_source_label(
        const std::unordered_map<std::string, std::string>& source_payload) const override;

private:
    std::string       _name;
    CorpusConfig      _corpus;
    LegislationConfig _legislation;
    std::vector<LegislationSource> _leg_sources;
};

} // namespace astraea::nz_tenancy
