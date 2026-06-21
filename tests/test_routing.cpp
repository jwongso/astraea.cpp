// Routing engine tests - ported from Python .training/test_local_fixtures.py
// and core/routing.py unit tests.
//
// Route definitions are minimal subsets of the real NZ tenancy routes, chosen
// to exercise the same trigger paths as the Python fixture suite.
#include "astraea/routing.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>

using namespace astraea;

// ---------------------------------------------------------------------------
// Minimal NZ-tenancy-flavoured route table for fixture testing
// ---------------------------------------------------------------------------

static const std::vector<StatuteRoute> TEST_ROUTES = {
    {
        .intent = "repairs_maintenance",
        .include_any = {
            "not working", "broken", "won't fix", "hasn't fixed", "not fixed",
            "maintenance", "heat pump", "heatpump",
            "mould", "mold", "damp", "moisture", "leak", "leaking",
            "hot water", "no hot water", "heating", "no heating",
            "repair", "habitable", "s45",
        },
        .exclude_any = {
            "healthy homes", "fair wear and tear", "wear and tear",
            "alteration", "minor change", "renovation",
        },
        .forced_sections = { "NZLEG/RTA/s45", "NZLEG/RTA/s33" },
        .synthetic_query = "landlord repair obligation section 45",
    },
    {
        .intent = "property_change",
        .include_any_precise = {
            "alteration", "minor change",
            "drawing pin", "drawing pins",
            "command strip", "command strips",
            "picture hook", "picture hooks",
            "security camera", "cctv",
            "without consent", "without permission",
            "landlord consent", "written consent",
        },
        .include_any_broad = { "install", "installed", "fixture", "fence", "improvement" },
        .require_context_any = { "consent", "permission", "alteration", "improvement" },
        .exclude_any = {
            "healthy homes", "not repaired", "not fixed", "won't fix",
            "broken", "not working",
        },
        .forced_sections = { "NZLEG/RTA/s40", "NZLEG/RTA/s42A", "NZLEG/RTA/s42B" },
        .leg_allow_list  = { "NZLEG/RTA/s40", "NZLEG/RTA/s42A", "NZLEG/RTA/s42B" },
        .synthetic_query = "tenant alteration consent fixtures",
        .priority = 10,
    },
    {
        .intent = "agreement_form",
        .include_any = {
            "tenancy agreement", "written agreement",
            "pet clause", "pets allowed", "cats allowed", "dogs allowed",
            "cat allowed", "dog allowed", "allow pets", "allow cats",
            "no pets", "have a cat", "have a dog",
        },
        .forced_sections = { "NZLEG/RTA/s13A" },
        .synthetic_query = "tenancy agreement form contents pets",
    },
    {
        .intent = "landlord_entry",
        .include_any = {
            "landlord entry", "inspection notice", "24 hour notice",
            "inspection report", "inspection reports",
            "routine inspection", "routine inspections",
            "entered my home", "right of entry",
            "entered without notice",
            "notice to enter", "s48",
        },
        .forced_sections = { "NZLEG/RTA/s48" },
        .synthetic_query = "landlord entry inspection notice section 48",
    },
    {
        .intent = "healthy_homes",
        .include_any = {
            "healthy homes", "healthy home", "hhs",
            "heating standard", "insulation standard",
            "ventilation standard", "moisture barrier", "draught",
        },
        .forced_sections = { "NZLEG/RTA/s138B" },
        .synthetic_query = "healthy homes standards",
    },
};

// ---------------------------------------------------------------------------
// normalize_query
// ---------------------------------------------------------------------------

TEST_CASE("normalize_query: basic lowercase and whitespace", "[normalize]") {
    REQUIRE(normalize_query("  Hello World  ") == "hello world");
    REQUIRE(normalize_query("UPPER\t CASE\n") == "upper case");
}

// Raw UTF-8 byte helpers for Unicode test strings
static std::string utf8(std::initializer_list<unsigned char> bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

TEST_CASE("normalize_query: curly quotes to ASCII", "[normalize]") {
    // U+2018 U+2019 (left/right single quotation marks) -> '
    const std::string lsq = utf8({0xe2, 0x80, 0x98}); // U+2018
    const std::string rsq = utf8({0xe2, 0x80, 0x99}); // U+2019
    REQUIRE(normalize_query(lsq + "quoted" + rsq) == "'quoted'");

    // U+201C U+201D (left/right double quotation marks) -> "
    const std::string ldq = utf8({0xe2, 0x80, 0x9c}); // U+201C
    const std::string rdq = utf8({0xe2, 0x80, 0x9d}); // U+201D
    REQUIRE(normalize_query(ldq + "quoted" + rdq) == "\"quoted\"");
}

TEST_CASE("normalize_query: dashes to space", "[normalize]") {
    REQUIRE(normalize_query("well-known") == "well known");
    // U+2014 em dash = E2 80 94
    const std::string em = utf8({0xe2, 0x80, 0x94});
    REQUIRE(normalize_query("before" + em + "after") == "before after");
    // U+2013 en dash = E2 80 93
    const std::string en = utf8({0xe2, 0x80, 0x93});
    REQUIRE(normalize_query("2020" + en + "2021") == "2020 2021");
}

TEST_CASE("normalize_query: multi-space collapse", "[normalize]") {
    REQUIRE(normalize_query("a   b   c") == "a b c");
}

TEST_CASE("normalize_query: empty input", "[normalize]") {
    REQUIRE(normalize_query("") == "");
    REQUIRE(normalize_query("   ") == "");
}

// ---------------------------------------------------------------------------
// Route fixture helpers
// ---------------------------------------------------------------------------

static bool has_route(const RouteDecision& d, const std::string& intent) {
    return std::find(d.matched_intents.begin(), d.matched_intents.end(), intent)
           != d.matched_intents.end();
}

// ---------------------------------------------------------------------------
// Route fixtures (ported from Python test_local_fixtures.py)
// ---------------------------------------------------------------------------

TEST_CASE("fixture: repairs_maintenance - heat pump fault", "[routing][fixture]") {
    const std::string q = "The heat pump has a strong burning smell and my landlord refuses to send a technician.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(d.triggered);
    REQUIRE(has_route(d, "repairs_maintenance"));
    REQUIRE_FALSE(has_route(d, "healthy_homes"));
}

TEST_CASE("fixture: property_change - drawing pins and command strips", "[routing][fixture]") {
    const std::string q = "My landlord says I cannot use drawing pins or command strips to hang photos on the walls. Can they enforce this?";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(d.triggered);
    REQUIRE(has_route(d, "property_change"));
    REQUIRE_FALSE(has_route(d, "repairs_maintenance"));
}

TEST_CASE("fixture: landlord_entry - inspection report", "[routing][fixture]") {
    const std::string q = "My property manager says they do not provide inspection reports to tenants after routine inspections. Is that legal?";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(d.triggered);
    REQUIRE(has_route(d, "landlord_entry"));
    REQUIRE_FALSE(has_route(d, "repairs_maintenance"));
}

TEST_CASE("fixture: agreement_form - pet clause", "[routing][fixture]") {
    const std::string q = "My tenancy agreement says cats are allowed but the property manager is now saying I cannot have my cat indoors.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(d.triggered);
    REQUIRE(has_route(d, "agreement_form"));
    REQUIRE_FALSE(has_route(d, "repairs_maintenance"));
}

TEST_CASE("fixture: regression - 'tenants' must not match 'ants' in repairs_maintenance", "[routing][fixture]") {
    const std::string q = "I am a tenant and I need to know my rights regarding a routine inspection letter I received.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(d.triggered);
    REQUIRE(has_route(d, "landlord_entry"));
    REQUIRE_FALSE(has_route(d, "repairs_maintenance"));
}

// ---------------------------------------------------------------------------
// RouteDecision field correctness
// ---------------------------------------------------------------------------

TEST_CASE("RouteDecision: forced sections union", "[routing]") {
    // Draw both repairs_maintenance and landlord_entry - forced sections must be unioned
    const std::string q = "The landlord entered without notice and the heat pump is broken.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(has_route(d, "repairs_maintenance"));
    REQUIRE(has_route(d, "landlord_entry"));

    auto& f = d.forced_sections;
    REQUIRE(std::find(f.begin(), f.end(), "NZLEG/RTA/s45") != f.end());
    REQUIRE(std::find(f.begin(), f.end(), "NZLEG/RTA/s48") != f.end());
}

TEST_CASE("RouteDecision: triggered=false when no terms match", "[routing]") {
    const std::string q = "I would like to bake a cake this weekend.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE_FALSE(d.triggered);
    REQUIRE(d.matched_intents.empty());
    REQUIRE(d.forced_sections.empty());
}

TEST_CASE("RouteDecision: exclude_any suppresses route", "[routing]") {
    // "maintenance" is in repairs_maintenance include_any, but
    // "healthy homes" is in its exclude_any - route must not fire.
    const std::string q = "What are the healthy homes maintenance requirements for rental properties?";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE_FALSE(has_route(d, "repairs_maintenance"));
    REQUIRE(has_route(d, "healthy_homes"));
}

TEST_CASE("RouteDecision: two-tier precise fires without context", "[routing]") {
    // "drawing pins" is in include_any_precise - fires regardless of require_context_any
    const std::string q = "Can I use drawing pins to hang art?";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(has_route(d, "property_change"));
    // trigger path must be "precise"
    bool found_precise = false;
    for (const auto& tp : d.trigger_paths)
        if (tp.intent == "property_change" && tp.path == "precise")
            found_precise = true;
    REQUIRE(found_precise);
}

TEST_CASE("RouteDecision: two-tier broad requires context", "[routing]") {
    // "fence" is in include_any_broad; "consent" is in require_context_any
    const std::string q = "I want to install a fence.";
    // No context word - broad must NOT fire
    auto d_no_ctx = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE_FALSE(has_route(d_no_ctx, "property_change"));

    const std::string q2 = "I want to install a fence. Do I need permission?";
    auto d_ctx = build_route_decision(q2, q2, TEST_ROUTES);
    REQUIRE(has_route(d_ctx, "property_change"));
    bool found_broad = false;
    for (const auto& tp : d_ctx.trigger_paths)
        if (tp.intent == "property_change" && tp.path == "broad+context")
            found_broad = true;
    REQUIRE(found_broad);
}

TEST_CASE("RouteDecision: leg_allow_list union from all routes with one", "[routing]") {
    // property_change has a leg_allow_list; repairs_maintenance does not.
    // When both fire, the union of all allow-lists is returned. Here only
    // property_change defines one, so the result equals its list.
    const std::string q = "The drawing pins made a hole and now the wall is not repaired.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    if (has_route(d, "property_change")) {
        auto& al = d.leg_allow_list;
        REQUIRE(std::find(al.begin(), al.end(), "NZLEG/RTA/s40") != al.end());
    }
}

TEST_CASE("RouteDecision: leg_allow_list unions sections from all matched routes",
          "[routing]") {
    // Two routes both define leg_allow_lists. Under union semantics every
    // matched route contributes; the dominant route does NOT block the
    // lower-priority route's sections (it only controls framing).
    //
    // Pre-PR: this same fixture asserted the OPPOSITE — that s99 must NOT
    // appear. That behaviour was a port slip from the Python reference and
    // forced O(N^2) hand-stitching of every co-firing pair. The "must not
    // bleed through" assertion is now the wrong contract; the test below
    // pins the correct one.
    static const std::vector<StatuteRoute> dual = {
        {
            .intent = "route_high",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s1", "NZLEG/ACT/s2" },
            .priority = 10,
        },
        {
            .intent = "route_low",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s99" },
            .priority = 0,
        },
    };
    auto d = build_route_decision("alpha", "alpha", dual);
    REQUIRE(has_route(d, "route_high"));
    REQUIRE(has_route(d, "route_low"));
    const auto& al = d.leg_allow_list;
    REQUIRE(std::find(al.begin(), al.end(), "NZLEG/ACT/s1")  != al.end());
    REQUIRE(std::find(al.begin(), al.end(), "NZLEG/ACT/s2")  != al.end());
    // route_low's section MUST be reachable under union semantics.
    REQUIRE(std::find(al.begin(), al.end(), "NZLEG/ACT/s99") != al.end());

    // Dominance is still tracked, but only for framing (synthetic queries,
    // rule cards, debug fields). It does NOT decide section visibility.
    REQUIRE(d.dominant_route == "route_high");
}

TEST_CASE("RouteDecision: leg_allow_list union is deduplicated and order-preserved",
          "[routing]") {
    // Overlapping allow-lists across two matched routes: each section must
    // appear once, in first-occurrence order across the route fire sequence.
    static const std::vector<StatuteRoute> dual = {
        {
            .intent = "route_a",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s1", "NZLEG/ACT/s2" },
            .priority = 10,
        },
        {
            .intent = "route_b",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s2", "NZLEG/ACT/s3" }, // s2 overlaps
            .priority = 5,
        },
    };
    auto d = build_route_decision("alpha", "alpha", dual);
    const auto& al = d.leg_allow_list;
    REQUIRE(al.size() == 3);
    // First-occurrence order: s1, s2, s3.
    REQUIRE(al[0] == "NZLEG/ACT/s1");
    REQUIRE(al[1] == "NZLEG/ACT/s2");
    REQUIRE(al[2] == "NZLEG/ACT/s3");
}

TEST_CASE("RouteDecision: dominant route controls framing under union",
          "[routing]") {
    // Even though allow-lists are unioned, dominant_route still names the
    // highest-priority allow-list-defining route. Framing concerns
    // (synthetic-query priority, rule-card ordering, debug dominance) are
    // intentionally separated from section visibility.
    static const std::vector<StatuteRoute> dual = {
        {
            .intent = "route_low",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s99" },
            .synthetic_query = "low priority synthetic",
            .priority = 1,
        },
        {
            .intent = "route_high",
            .include_any = { "alpha" },
            .leg_allow_list = { "NZLEG/ACT/s1" },
            .synthetic_query = "high priority synthetic",
            .priority = 20,
        },
    };
    auto d = build_route_decision("alpha", "alpha", dual);
    REQUIRE(d.dominant_route == "route_high");
    // Both synthetic queries collected (anchor.cpp uses them in fire order;
    // the priority signal lives in dominant_route, not in the queries list).
    REQUIRE(d.leg_synthetic_queries.size() == 2);
}

TEST_CASE("RouteDecision: synthetic queries collected", "[routing]") {
    const std::string q = "The heat pump is not working and the landlord won't fix it.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(has_route(d, "repairs_maintenance"));
    const auto& sq = d.leg_synthetic_queries;
    REQUIRE(std::find(sq.begin(), sq.end(), "landlord repair obligation section 45") != sq.end());
}

TEST_CASE("RouteDecision: boosted_act_ids from forced sections", "[routing]") {
    // NZLEG/RTA/s45 -> act_id = "RTA"
    const std::string q = "The hot water is broken and the landlord won't fix it.";
    auto d = build_route_decision(q, q, TEST_ROUTES);
    REQUIRE(has_route(d, "repairs_maintenance"));
    REQUIRE(d.boosted_act_ids.count("RTA") == 1);
}

// ---------------------------------------------------------------------------
// compute_suppressed_lp_ids - precompute helper for the LP gate
// ---------------------------------------------------------------------------

TEST_CASE("compute_suppressed_lp_ids: single-pass precompute, boundary-aware",
          "[routing]") {
    using LP = std::vector<std::pair<std::string, std::vector<std::string>>>;
    const LP lp = {
        {"SEC/A", {"meth", "methamphetamine"}},
        {"SEC/B", {"assault", "harm"}},
        {"SEC/C", {"only specific phrase here"}},
    };

    // No vocabulary -> all three suppressed.
    auto s = compute_suppressed_lp_ids("the landlord has not fixed the heating", lp);
    REQUIRE(s.size() == 3);
    REQUIRE(s.contains("SEC/A"));
    REQUIRE(s.contains("SEC/B"));
    REQUIRE(s.contains("SEC/C"));

    // "method" must NOT open SEC/A - boundary check rejects "meth" inside "method".
    s = compute_suppressed_lp_ids("what is the best method", lp);
    REQUIRE(s.contains("SEC/A"));
    REQUIRE(s.contains("SEC/B"));

    // Real meth phrase opens SEC/A only.
    s = compute_suppressed_lp_ids("meth contamination report", lp);
    REQUIRE_FALSE(s.contains("SEC/A"));
    REQUIRE(s.contains("SEC/B"));
    REQUIRE(s.contains("SEC/C"));

    // Multi-word LP phrase opens its group when present verbatim.
    s = compute_suppressed_lp_ids("only specific phrase here, please", lp);
    REQUIRE_FALSE(s.contains("SEC/C"));
}

TEST_CASE("compute_suppressed_lp_ids: equivalent to per-call allow_section",
          "[routing]") {
    // The whole point of the refactor: replacing N allow_section() calls with
    // one precompute + N set lookups must not change behavior.
    using LP = std::vector<std::pair<std::string, std::vector<std::string>>>;
    const LP lp = {
        {"SEC/A", {"meth", "methamphetamine"}},
        {"SEC/B", {"assault", "harm"}},
        {"SEC/C", {"only specific phrase here"}},
    };
    const std::vector<std::string> queries = {
        "the landlord has not fixed the heating",
        "what is the best method",
        "meth contamination report",
        "physical assault by ex partner",
        "the building has charm and pharmacy nearby",
        "only specific phrase here today",
    };
    const std::vector<std::string> ids = {"SEC/A", "SEC/B", "SEC/C", "SEC/D"};
    for (const auto& q : queries) {
        const auto s = compute_suppressed_lp_ids(q, lp);
        for (const auto& id : ids) {
            const bool allowed_via_set    = !s.contains(id);
            const bool allowed_via_legacy = allow_section(id, q, lp);
            INFO("q=" << q << "  id=" << id);
            REQUIRE(allowed_via_set == allowed_via_legacy);
        }
    }
}
