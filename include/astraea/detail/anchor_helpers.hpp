#pragma once
// Internal helpers for anchor.cpp — also included by test_pipeline_helpers.cpp.
// All functions are pure (no I/O, no Drogon) and therefore testable without mocks.
#include "astraea/retriever_types.hpp"
#include <cctype>
#include <string_view>

namespace astraea::detail {

// Returns true if the case_id prefix (before the first '/') contains "LEG"
// (case-insensitive). Identifies legislation chunks in mixed collections.
// Examples: "NZLEG/001" -> true, "NZT-001/005" -> false, "no-slash" -> false.
inline bool is_leg_chunk(std::string_view case_id) {
    const auto slash = case_id.find('/');
    if (slash == std::string_view::npos) return false;
    const auto prefix = case_id.substr(0, slash);
    for (size_t i = 0; i + 3 <= prefix.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(prefix[i]))     == 'L' &&
            std::toupper(static_cast<unsigned char>(prefix[i + 1])) == 'E' &&
            std::toupper(static_cast<unsigned char>(prefix[i + 2])) == 'G')
            return true;
    }
    return false;
}

} // namespace astraea::detail
