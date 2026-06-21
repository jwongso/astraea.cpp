#pragma once
// Internal helpers for anchor.cpp — also included by test_pipeline_helpers.cpp.
// All functions are pure (no I/O, no Drogon) and therefore testable without mocks.
#include "astraea/retriever_types.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace astraea::detail {

/// @brief Return true when the case_id prefix (before the first '/') contains "LEG" (case-insensitive).
///
/// Identifies legislation chunks in mixed collections where legislation and
/// case-law share the same Qdrant collection. Examples: "NZLEG/001" returns
/// true; "NZT-001/005" and "no-slash" return false.
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

/// @brief Compute the legislation-anchor hit cap for a single request.
///
/// Three regimes:
///   Unrouted  (n_forced == 0)                          -> 2  (flat top-k, no forced sections)
///   Weak path (n_forced > 0, allow_list_size == 0)     -> max(3, n_forced)
///   Strong path (n_forced > 0, allow_list_size > 0)    -> min(allow_list_size, max(3, n_forced))
///
/// The strong-path formula bounds the cap at the size of the whitelisted
/// universe so we never allocate similarity slots for sections outside
/// the allow-list (they were already stripped by the allow-list filter).
///
/// Invariant: when n_forced == 0 the result is always 2, which callers
/// can assert as evidence that the unrouted path has not silently adopted
/// the routed path's cap shape.
inline int compute_max_hits(int n_forced, std::size_t allow_list_size) noexcept {
    if (n_forced == 0) return 2;
    const int floor = std::max(3, n_forced);
    if (allow_list_size == 0) return floor;
    return static_cast<int>(std::min(allow_list_size, static_cast<std::size_t>(floor)));
}

/// @brief Policy for reconcile_max_hits when n_forced > max_hits.
///
/// Throw   - debug/test builds: surface authoring slips loudly (Catch2 catches).
/// LogAndClamp - production: widen the cap so forced sections are not silently
///               truncated. Caller is expected to have already logged the slip;
///               this helper is pure (no I/O) so it stays unit-testable.
enum class MaxHitsCapViolation : std::uint8_t { Throw, LogAndClamp };

/// @brief Enforce n_forced <= max_hits, with build-tunable failure mode.
///
/// The invariant exists because forced_sections are sections a route declared
/// mandatory: silently truncating them is the worst-case data-loss path in
/// retrieval (the model receives less context than the route author promised).
/// In Release builds the assert() that previously guarded this was compiled
/// out by NDEBUG, masking the bug entirely.
///
/// @return The reconciled cap. Equal to max_hits when the invariant holds.
///         When violated and policy is LogAndClamp, returns n_forced (the
///         minimum cap that preserves every forced section).
/// @throws std::logic_error when n_forced > max_hits and policy is Throw.
inline int reconcile_max_hits(int n_forced, int max_hits, MaxHitsCapViolation policy) {
    if (n_forced <= max_hits) return max_hits;
    if (policy == MaxHitsCapViolation::Throw) {
        throw std::logic_error(
            "retrieve_anchor invariant violated: n_forced (" + std::to_string(n_forced) +
            ") > max_hits (" + std::to_string(max_hits) + ")");
    }
    return n_forced;
}

} // namespace astraea::detail
