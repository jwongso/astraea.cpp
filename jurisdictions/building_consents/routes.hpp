#pragma once
#include "astraea/routing.hpp"
#include <span>

namespace astraea::nz_building {

// 17 statute routes for NZ building consents (Building Act 2004 + EBWO 2020).
std::span<const StatuteRoute> get_routes();

} // namespace astraea::nz_building
