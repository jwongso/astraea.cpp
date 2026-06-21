// Unit tests for the pure forced-section validator. No Drogon, no Qdrant.
// The caller supplies an `exists(id)` predicate; we verify that the validator
// walks every route's forced_sections plus the LOW_PRIORITY_SECTIONS keys,
// reports every missing (section, declarer) pair, and dedupes correctly.
#include "astraea/route_validator.hpp"
#include "astraea/jurisdiction.hpp"
#include <catch2/catch_all.hpp>
#include <unordered_set>

using namespace astraea;

namespace {

// Test jurisdiction: configurable routes + LP map, everything else trivial.
class FakeJur final : public JurisdictionBase {
public:
    FakeJur(std::vector<StatuteRoute> rs,
            std::vector<std::pair<std::string, std::vector<std::string>>> lps)
        : _routes(std::move(rs)), _lp(std::move(lps)) {}

    const std::string& name() const override { return _name; }
    std::string description() const override { return ""; }
    const CorpusConfig& corpus() const override { return _corpus; }
    const std::string& system_prompt() const override { return _sp; }
    std::span<const StatuteRoute> routes() const override {
        return std::span<const StatuteRoute>{_routes.data(), _routes.size()};
    }
    std::optional<LegislationConfig> legislation() const override { return std::nullopt; }
    const std::vector<LegislationSource>& leg_sources() const override { return _src; }
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
            low_priority_sections() const override { return _lp; }

private:
    std::string _name = "test";
    CorpusConfig _corpus{};
    std::string _sp;
    std::vector<StatuteRoute> _routes;
    std::vector<std::pair<std::string, std::vector<std::string>>> _lp;
    std::vector<LegislationSource> _src;
};

} // namespace

TEST_CASE("validate_forced_sections: empty routes returns clean report",
          "[route_validator]") {
    FakeJur j({}, {});
    auto r = validate_forced_sections(j, [](std::string_view) { return true; });
    REQUIRE(r.missing.empty());
    REQUIRE(r.routes_checked == 0);
    REQUIRE(r.sections_checked == 0);
}

TEST_CASE("validate_forced_sections: all sections present -> empty missing",
          "[route_validator]") {
    FakeJur j({
        {.intent = "route_a", .forced_sections = {"NZLEG/RTA/s40"}},
        {.intent = "route_b", .forced_sections = {"NZLEG/RTA/s18"}},
    }, {
        {"NZLEG/RTA/s49A", {"meth"}},
    });
    auto r = validate_forced_sections(j, [](std::string_view) { return true; });
    REQUIRE(r.missing.empty());
    REQUIRE(r.routes_checked == 2);
    REQUIRE(r.sections_checked == 3);
}

TEST_CASE("validate_forced_sections: missing section is reported per declarer",
          "[route_validator]") {
    // Both routes force s35; LP map gates s55C. The corpus lookup says
    // both are missing — every declarer must surface independently in
    // the report (not just the first).
    FakeJur j({
        {.intent = "route_a", .forced_sections = {"NZLEG/RTA/s40", "NZLEG/RTA/s35"}},
        {.intent = "route_b", .forced_sections = {"NZLEG/RTA/s35"}},
    }, {
        {"NZLEG/RTA/s55C", {"early exit"}},
    });
    const std::unordered_set<std::string> absent = {
        "NZLEG/RTA/s35", "NZLEG/RTA/s55C",
    };
    auto r = validate_forced_sections(j, [&](std::string_view sid) {
        return absent.find(std::string{sid}) == absent.end();
    });

    REQUIRE(r.routes_checked == 2);
    REQUIRE(r.sections_checked == 3);
    REQUIRE(r.missing.size() == 3); // s35×2 + s55C×1

    auto find = [&](const std::string& sid, const std::string& decl) {
        return std::find_if(r.missing.begin(), r.missing.end(),
            [&](const MissingSection& m) {
                return m.section_id == sid && m.declared_by == decl;
            });
    };
    REQUIRE(find("NZLEG/RTA/s35",  "route_a") != r.missing.end());
    REQUIRE(find("NZLEG/RTA/s35",  "route_b") != r.missing.end());
    auto lp_entry = find("NZLEG/RTA/s55C", "low_priority_sections");
    REQUIRE(lp_entry != r.missing.end());
    REQUIRE(lp_entry->source == SectionSource::LowPrioritySection);
}

TEST_CASE("validate_forced_sections: lookup called once per distinct id",
          "[route_validator]") {
    // The same section ID forced by multiple routes must not trigger
    // multiple lookups — the corpus probe is the expensive part and
    // should be deduped by the validator.
    FakeJur j({
        {.intent = "a", .forced_sections = {"X", "Y"}},
        {.intent = "b", .forced_sections = {"X", "Z"}},
        {.intent = "c", .forced_sections = {"X"}},
    }, {
        {"Y", {"term"}},      // Y appears as both forced AND LP key — still one lookup
    });
    std::unordered_map<std::string, int> counts;
    auto r = validate_forced_sections(j, [&](std::string_view sid) {
        counts[std::string{sid}]++;
        return true;
    });
    REQUIRE(counts["X"] == 1);
    REQUIRE(counts["Y"] == 1);
    REQUIRE(counts["Z"] == 1);
    REQUIRE(r.sections_checked == 3);
}

TEST_CASE("validate_forced_sections: missing list ordering is stable",
          "[route_validator]") {
    FakeJur j({
        {.intent = "zeta",   .forced_sections = {"NZLEG/RTA/s99"}},
        {.intent = "alpha",  .forced_sections = {"NZLEG/RTA/s10"}},
        {.intent = "middle", .forced_sections = {"NZLEG/RTA/s50"}},
    }, {});
    auto r = validate_forced_sections(j, [](std::string_view) { return false; });
    REQUIRE(r.missing.size() == 3);
    // Sorted by section_id then declared_by.
    REQUIRE(r.missing[0].section_id == "NZLEG/RTA/s10");
    REQUIRE(r.missing[1].section_id == "NZLEG/RTA/s50");
    REQUIRE(r.missing[2].section_id == "NZLEG/RTA/s99");
}
