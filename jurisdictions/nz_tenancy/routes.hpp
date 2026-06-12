#pragma once
#include "astraea/routing.hpp"
#include <span>
#include <utility>
#include <vector>

namespace astraea::nz_tenancy {

// All 20 statute routes for NZ residential tenancy law (RTA 1986).
std::span<const StatuteRoute> get_routes();

// Low-priority section gate - same semantics as Python LOW_PRIORITY_SECTIONS.
// Pass to allow_section() to suppress rarely-relevant sections unless the
// question contains their specific vocabulary.
const std::vector<std::pair<std::string, std::vector<std::string>>>&
get_low_priority_sections();

} // namespace astraea::nz_tenancy
