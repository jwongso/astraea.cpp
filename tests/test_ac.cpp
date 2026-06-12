// Aho-Corasick unit tests
#include "astraea/aho_corasick.hpp"
#include "astraea/routing.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>

using namespace astraea;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static StatuteRoute make_route(
    std::string intent,
    std::vector<std::string> include_any = {},
    std::vector<std::string> exclude_any = {},
    std::vector<std::string> precise = {},
    std::vector<std::string> broad = {},
    std::vector<std::string> ctx = {},
    std::vector<std::string> include_all = {})
{
    StatuteRoute r;
    r.intent           = std::move(intent);
    r.include_any      = std::move(include_any);
    r.exclude_any      = std::move(exclude_any);
    r.include_any_precise  = std::move(precise);
    r.include_any_broad    = std::move(broad);
    r.require_context_any  = std::move(ctx);
    r.include_all      = std::move(include_all);
    return r;
}

static bool hit_for(const std::vector<AcHit>& hits, int ri, AcField f, std::string_view t) {
    return std::any_of(hits.begin(), hits.end(), [&](const AcHit& h) {
        return h.route_idx == ri && h.field == f && h.term == t;
    });
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("AhoCorasick: empty routes => empty automaton", "[ac]") {
    const std::vector<StatuteRoute> empty;
    AhoCorasick ac(empty);
    REQUIRE(ac.empty());
    REQUIRE(ac.search("anything").empty());
}

TEST_CASE("AhoCorasick: single term found", "[ac]") {
    const std::vector<StatuteRoute> routes = { make_route("r0", {"heat pump"}) };
    AhoCorasick ac(routes);
    REQUIRE_FALSE(ac.empty());

    auto hits = ac.search("the heat pump is broken");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "heat pump"));
}

TEST_CASE("AhoCorasick: term not present gives no hit", "[ac]") {
    const std::vector<StatuteRoute> routes = { make_route("r0", {"mould"}) };
    AhoCorasick ac(routes);
    REQUIRE(ac.search("the heat pump is broken").empty());
}

// ---------------------------------------------------------------------------
// Multi-term / multi-route
// ---------------------------------------------------------------------------

TEST_CASE("AhoCorasick: multiple terms in one route", "[ac]") {
    const std::vector<StatuteRoute> routes = {
        make_route("repairs", {"heat pump", "mould", "leak"})
    };
    AhoCorasick ac(routes);
    auto hits = ac.search("there is mould and a leak");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "mould"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "leak"));
    REQUIRE_FALSE(hit_for(hits, 0, AcField::Legacy, "heat pump"));
}

TEST_CASE("AhoCorasick: two routes, independent matches", "[ac]") {
    const std::vector<StatuteRoute> routes = {
        make_route("repairs", {"heat pump", "mould"}),
        make_route("entry",   {"inspection", "notice to enter"}),
    };
    AhoCorasick ac(routes);
    auto hits = ac.search("mould and inspection found");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "mould"));
    REQUIRE(hit_for(hits, 1, AcField::Legacy, "inspection"));
    REQUIRE_FALSE(hit_for(hits, 0, AcField::Legacy, "heat pump"));
}

TEST_CASE("AhoCorasick: same term in two routes both reported", "[ac]") {
    const std::vector<StatuteRoute> routes = {
        make_route("r0", {"landlord"}),
        make_route("r1", {"landlord"}),
    };
    AhoCorasick ac(routes);
    auto hits = ac.search("landlord won't fix it");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "landlord"));
    REQUIRE(hit_for(hits, 1, AcField::Legacy, "landlord"));
}

// ---------------------------------------------------------------------------
// Field kinds
// ---------------------------------------------------------------------------

TEST_CASE("AhoCorasick: exclude field reported correctly", "[ac]") {
    StatuteRoute r = make_route("repairs", {"maintenance"}, {"healthy homes"});
    const std::vector<StatuteRoute> routes = {r};
    AhoCorasick ac(routes);
    auto hits = ac.search("healthy homes maintenance requirements");
    REQUIRE(hit_for(hits, 0, AcField::Exclude, "healthy homes"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "maintenance"));
}

TEST_CASE("AhoCorasick: precise and broad fields", "[ac]") {
    StatuteRoute r = make_route("property_change", {}, {}, {"drawing pins"}, {"install"}, {"consent"});
    const std::vector<StatuteRoute> routes = {r};
    AhoCorasick ac(routes);

    auto hits_precise = ac.search("can i use drawing pins on walls");
    REQUIRE(hit_for(hits_precise, 0, AcField::Precise, "drawing pins"));
    REQUIRE_FALSE(hit_for(hits_precise, 0, AcField::Broad, "install"));

    auto hits_broad = ac.search("i want to install a fence with consent");
    REQUIRE(hit_for(hits_broad, 0, AcField::Broad, "install"));
    REQUIRE(hit_for(hits_broad, 0, AcField::Context, "consent"));
}

TEST_CASE("AhoCorasick: include_all field", "[ac]") {
    StatuteRoute r = make_route("r0", {}, {}, {}, {}, {}, {"alpha", "beta"});
    const std::vector<StatuteRoute> routes = {r};
    AhoCorasick ac(routes);

    auto hits_both = ac.search("alpha and beta together");
    REQUIRE(hit_for(hits_both, 0, AcField::All, "alpha"));
    REQUIRE(hit_for(hits_both, 0, AcField::All, "beta"));

    auto hits_one = ac.search("only alpha here");
    REQUIRE(hit_for(hits_one, 0, AcField::All, "alpha"));
    REQUIRE_FALSE(hit_for(hits_one, 0, AcField::All, "beta"));
}

// ---------------------------------------------------------------------------
// Overlapping patterns - AC's core correctness property
// ---------------------------------------------------------------------------

TEST_CASE("AhoCorasick: overlapping patterns (he/she/his/hers)", "[ac]") {
    const std::vector<StatuteRoute> routes = {
        make_route("r0", {"he", "she", "his", "hers"}),
    };
    AhoCorasick ac(routes);
    auto hits = ac.search("ushers");
    // "she" at pos 1, "he" at pos 2, "hers" at pos 2, "his" not present
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "she"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "he"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "hers"));
    REQUIRE_FALSE(hit_for(hits, 0, AcField::Legacy, "his"));
}

// ---------------------------------------------------------------------------
// Integration with build_route_decision (AC-powered path)
// ---------------------------------------------------------------------------

TEST_CASE("AC integration: repairs route fires via legacy", "[ac][routing]") {
    const std::vector<StatuteRoute> routes = {
        make_route("repairs", {"heat pump", "mould", "leak"})
    };
    auto d = build_route_decision("the heat pump is broken", "heat pump broken", routes);
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(), "repairs")
            != d.matched_intents.end());
}

TEST_CASE("AC integration: exclude suppresses route", "[ac][routing]") {
    const std::vector<StatuteRoute> routes = {
        make_route("repairs", {"maintenance"}, {"healthy homes"})
    };
    auto d = build_route_decision("healthy homes maintenance", "", routes);
    REQUIRE_FALSE(d.triggered);
}

TEST_CASE("AC integration: precise fires without context", "[ac][routing]") {
    const std::vector<StatuteRoute> routes = {
        make_route("changes", {}, {}, {"drawing pins"}, {"install"}, {"consent"})
    };
    auto d = build_route_decision("can i use drawing pins", "", routes);
    REQUIRE(d.triggered);
    bool found = false;
    for (const auto& tp : d.trigger_paths)
        if (tp.intent == "changes" && tp.path == "precise") found = true;
    REQUIRE(found);
}

TEST_CASE("AC integration: broad fires only with context", "[ac][routing]") {
    const std::vector<StatuteRoute> routes = {
        make_route("changes", {}, {}, {"drawing pins"}, {"install"}, {"consent"})
    };
    auto no_ctx = build_route_decision("i want to install a fence", "", routes);
    REQUIRE_FALSE(no_ctx.triggered);

    auto with_ctx = build_route_decision("i want to install a fence with consent", "", routes);
    REQUIRE(with_ctx.triggered);
    bool found = false;
    for (const auto& tp : with_ctx.trigger_paths)
        if (tp.intent == "changes" && tp.path == "broad+context") found = true;
    REQUIRE(found);
}

// ---------------------------------------------------------------------------
// _insert edge cases (regression guards)
// ---------------------------------------------------------------------------

TEST_CASE("AhoCorasick: empty term must not match anywhere", "[ac][regression]") {
    // An empty term attached to the root would fire on every byte position.
    // Verify that an empty term is rejected and produces no false matches.
    const std::vector<StatuteRoute> routes = {
        make_route("r0", {"", "real term"})
    };
    AhoCorasick ac(routes);

    auto hits_empty = ac.search("");
    REQUIRE(hits_empty.empty());

    auto hits_text = ac.search("some unrelated text");
    // Only the real term could match; empty term must not produce hits.
    for (const auto& h : hits_text)
        REQUIRE(h.term != std::string_view{});

    // Sanity check the non-empty sibling still works.
    auto hits_real = ac.search("this is a real term here");
    REQUIRE(hit_for(hits_real, 0, AcField::Legacy, "real term"));
}

TEST_CASE("AhoCorasick: non-ASCII term skipped cleanly", "[ac][regression]") {
    // Terms containing non-ASCII bytes are rejected whole. Previously, _insert
    // would walk the ASCII prefix and return mid-loop, leaving dead trie nodes
    // (e.g. "c", "ca", "caf" prefixes for a "café" term). Compare the trie
    // size against a baseline that has only the ASCII term.
    const std::vector<StatuteRoute> baseline = { make_route("r0", {"tenant"}) };
    const std::vector<StatuteRoute> with_unicode = {
        make_route("r0", {"caf\xc3\xa9", "tenant"})  // "café" + "tenant"
    };
    AhoCorasick ac_base(baseline);
    AhoCorasick ac_uni(with_unicode);

    // No dead prefix nodes from "café" → both tries are identical in size.
    REQUIRE(ac_uni.node_count() == ac_base.node_count());

    // And matching still works for the surviving ASCII term.
    auto hits = ac_uni.search("the tenant visited the cafe today");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "tenant"));
}
