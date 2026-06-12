// RouteTable + cached-AC build_route_decision tests.
#include "astraea/route_table.hpp"
#include "astraea/routing.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>
#include <utility>

using namespace astraea;

// ---------------------------------------------------------------------------
// Fixture routes — same shape as the routing.cpp tests, kept minimal so the
// AC build is cheap and the assertions stay readable.
// ---------------------------------------------------------------------------

static const std::vector<StatuteRoute> TEST_ROUTES = {
    {
        .intent = "repairs",
        .include_any = { "heat pump", "leak", "broken", "won't fix" },
        .forced_sections = { "NZLEG/RTA/s45" },
        .synthetic_query = "landlord repair",
    },
    {
        .intent = "entry",
        .include_any = { "without notice", "entered my home", "inspection" },
        .forced_sections = { "NZLEG/RTA/s48" },
        .synthetic_query = "landlord entry",
    },
    {
        .intent = "bond",
        .include_any = { "bond", "lodged", "deposit" },
        .forced_sections = { "NZLEG/RTA/s19" },
        .synthetic_query = "bond return",
    },
};

// ---------------------------------------------------------------------------
// Construction & shape
// ---------------------------------------------------------------------------

TEST_CASE("RouteTable: empty routes -> empty AC, untriggered decision", "[route_table]") {
    const std::vector<StatuteRoute> empty;
    RouteTable table{empty};
    REQUIRE(table.routes().empty());
    REQUIRE(table.ac().empty());

    auto d = build_route_decision("anything", "", table);
    REQUIRE_FALSE(d.triggered);
    REQUIRE(d.matched_intents.empty());
}

TEST_CASE("RouteTable: non-empty routes -> non-empty AC", "[route_table]") {
    RouteTable table{TEST_ROUTES};
    REQUIRE(table.routes().size() == TEST_ROUTES.size());
    REQUIRE_FALSE(table.ac().empty());
    REQUIRE(table.ac().node_count() > 1); // at least root + at least one term
}

// ---------------------------------------------------------------------------
// AC is cached: the same AhoCorasick object is returned across calls.
// ---------------------------------------------------------------------------

TEST_CASE("RouteTable: ac() returns the same object across calls", "[route_table]") {
    RouteTable table{TEST_ROUTES};
    const AhoCorasick* a = &table.ac();
    const AhoCorasick* b = &table.ac();
    REQUIRE(a == b);

    // Two decision calls on the same table must not rebuild the AC. We cannot
    // observe construction directly, but we can verify the same backing object
    // is in use by checking node_count() is stable across calls.
    const auto nodes_before = table.ac().node_count();
    (void)build_route_decision("heat pump is broken", "", table);
    (void)build_route_decision("landlord entered my home", "", table);
    REQUIRE(table.ac().node_count() == nodes_before);
}

// ---------------------------------------------------------------------------
// Parity: RouteTable overload and span overload produce identical RouteDecision
// for the same inputs. This is the safety net against the refactor breaking
// observable behaviour.
// ---------------------------------------------------------------------------

static void require_decision_equal(const RouteDecision& a, const RouteDecision& b) {
    REQUIRE(a.triggered == b.triggered);

    auto sorted = [](std::vector<std::string> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    REQUIRE(sorted(a.matched_intents)        == sorted(b.matched_intents));
    REQUIRE(sorted(a.trigger_terms)          == sorted(b.trigger_terms));
    REQUIRE(sorted(a.forced_sections)        == sorted(b.forced_sections));
    REQUIRE(sorted(a.leg_allow_list)         == sorted(b.leg_allow_list));
    REQUIRE(sorted(a.leg_synthetic_queries)  == sorted(b.leg_synthetic_queries));
    REQUIRE(sorted(a.case_synthetic_queries) == sorted(b.case_synthetic_queries));

    const std::vector<std::string> a_boost(a.boosted_act_ids.begin(), a.boosted_act_ids.end());
    const std::vector<std::string> b_boost(b.boosted_act_ids.begin(), b.boosted_act_ids.end());
    REQUIRE(sorted(a_boost) == sorted(b_boost));

    REQUIRE(a.dominant_route   == b.dominant_route);
    REQUIRE(a.dominance_reason == b.dominance_reason);
}

TEST_CASE("RouteTable overload matches span overload byte-for-byte", "[route_table][parity]") {
    RouteTable table{TEST_ROUTES};

    const std::vector<std::string> queries = {
        "the heat pump is broken and the landlord won't fix it",
        "my landlord entered my home without notice during inspection",
        "the deposit was never lodged, where is my bond",
        "i would like to bake a cake this weekend",
        "",
    };

    for (const auto& q : queries) {
        auto via_span  = build_route_decision(q, q, std::span<const StatuteRoute>(TEST_ROUTES));
        auto via_table = build_route_decision(q, q, table);
        INFO("query: " << q);
        require_decision_equal(via_span, via_table);
    }
}

// ---------------------------------------------------------------------------
// Lifetime / move semantics
// ---------------------------------------------------------------------------

TEST_CASE("RouteTable: move-constructed table still produces correct decisions", "[route_table]") {
    RouteTable src{TEST_ROUTES};
    RouteTable dst{std::move(src)};

    auto d = build_route_decision("heat pump is broken", "", dst);
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(), "repairs")
            != d.matched_intents.end());
}

// Compile-time guards: the type must be non-copyable but movable. Catching
// accidental reintroduction of a copy constructor would otherwise only show
// up as a silent perf regression (large AC copied per call site).
static_assert(!std::is_copy_constructible_v<RouteTable>,
              "RouteTable must not be copyable (would clone the AC silently).");
static_assert(!std::is_copy_assignable_v<RouteTable>,
              "RouteTable must not be copy-assignable (would clone the AC silently).");
static_assert(std::is_move_constructible_v<RouteTable>,
              "RouteTable must remain move-constructible.");
static_assert(std::is_move_assignable_v<RouteTable>,
              "RouteTable must remain move-assignable.");
