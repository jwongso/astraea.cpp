#pragma once
// Startup-time validator: every section ID a route declares as
// forced_sections (or as a LOW_PRIORITY_SECTIONS gate key) must exist
// in the legislation corpus, otherwise the route is silently broken —
// the engine will inject nothing for that ID, and eval section_recall
// can never improve no matter how perfect the routing is.
//
// Pure validation logic lives here on top of a plain lookup callable
// so it stays unit-testable without Qdrant. The Qdrant-backed wrapper
// is in src/route_validator_corpus.cpp.
#include "astraea/jurisdiction.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace astraea {

class VectorStore;  // forward

/// @brief Where a section ID was declared. Drives the log message context.
enum class SectionSource : std::uint8_t {
    ForcedSection,        ///< StatuteRoute::forced_sections
    LowPrioritySection,   ///< LOW_PRIORITY_SECTIONS gate key
};

/// @brief One section ID a route declared that is absent from the corpus.
struct MissingSection {
    std::string   section_id;     ///< e.g. "NZLEG/RTA/s35"
    std::string   declared_by;    ///< route intent or "low_priority_sections"
    SectionSource source;
};

struct RouteValidationReport {
    std::vector<MissingSection> missing;
    std::size_t routes_checked   = 0;   ///< Routes inspected
    std::size_t sections_checked = 0;   ///< Distinct section IDs probed
};

/// @brief Pure validator. Walks every route's forced_sections plus the
/// LOW_PRIORITY_SECTIONS map keys, calls `exists(section_id)` once per
/// distinct ID, and records the (id, source, declared_by) tuples that
/// returned false.
///
/// The lookup callable is owned by the caller; in production it queries
/// Qdrant, in tests it consults a fixed set. Sections are probed once
/// per distinct ID; the same ID forced by multiple routes is only
/// looked up in the corpus once.
[[nodiscard]] RouteValidationReport validate_forced_sections(
    const JurisdictionBase& jurisdiction,
    const std::function<bool(std::string_view)>& exists);

/// @brief Live-Qdrant counterpart. Issues batched filtered_search
/// queries against the leg corpus, one slot per distinct section_id,
/// returns true for any section that has at least one chunk indexed.
///
/// Runs synchronously on its own EventLoopThread so it can be called
/// from main() before drogon::app().run() takes the main thread.
/// Blocks until every distinct ID has been probed or `timeout_s`
/// elapses (in which case unprobed IDs are conservatively NOT marked
/// missing — startup must not fail because the network was flaky).
[[nodiscard]] RouteValidationReport validate_forced_sections_against_corpus(
    const JurisdictionBase& jurisdiction,
    VectorStore& leg_store,
    double timeout_s = 10.0);

} // namespace astraea
