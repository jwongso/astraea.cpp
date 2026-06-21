#include "astraea/route_validator.hpp"
#include <algorithm>
#include <unordered_map>

namespace astraea {

RouteValidationReport validate_forced_sections(
    const JurisdictionBase& jurisdiction,
    const std::function<bool(std::string_view)>& exists)
{
    RouteValidationReport report;

    // Collect every distinct (section_id -> [declarers]) tuple so the
    // same missing section forced by multiple routes is reported once
    // per route that depends on it (not just the first).
    struct Decl {
        std::string declared_by;
        SectionSource source;
    };
    std::unordered_map<std::string, std::vector<Decl>> by_section;

    const auto routes = jurisdiction.routes();
    report.routes_checked = routes.size();
    for (const auto& r : routes) {
        for (const auto& sid : r.forced_sections) {
            by_section[sid].push_back({r.intent, SectionSource::ForcedSection});
        }
    }
    for (const auto& [sid, _terms] : jurisdiction.low_priority_sections()) {
        by_section[sid].push_back({"low_priority_sections",
                                   SectionSource::LowPrioritySection});
    }

    report.sections_checked = by_section.size();
    for (const auto& [sid, declarers] : by_section) {
        if (exists(sid)) continue;
        for (const auto& d : declarers) {
            report.missing.push_back({sid, d.declared_by, d.source});
        }
    }
    std::sort(report.missing.begin(), report.missing.end(),
              [](const MissingSection& a, const MissingSection& b) {
                  if (a.section_id != b.section_id) return a.section_id < b.section_id;
                  return a.declared_by < b.declared_by;
              });
    return report;
}

} // namespace astraea
