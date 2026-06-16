#pragma once
#include "astraea/jurisdiction.hpp"

namespace astraea::nz_building {

class NZBuildingJurisdiction final : public JurisdictionBase {
public:
    NZBuildingJurisdiction();

    const std::string&            name()          const override;
    std::string                   description()   const override;
    const CorpusConfig&           corpus()        const override;
    const std::string&            system_prompt() const override;
    std::span<const StatuteRoute> routes()        const override;

    const ConfidenceConfig& confidence_config() const override;

    std::optional<std::string> rewrite_prompt() const override;

    int   max_question_chars()  const override { return 1200; }
    bool  log_route_decisions() const override { return true; }

private:
    std::string    _name;
    CorpusConfig   _corpus;
    ConfidenceConfig _confidence;
};

} // namespace astraea::nz_building
