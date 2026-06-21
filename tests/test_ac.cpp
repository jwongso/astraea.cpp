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
// Word-boundary enforcement - the core safety property of the matcher
// ---------------------------------------------------------------------------

// These tests document and lock in the boundary-aware behavior introduced in
// this commit. The previous raw-substring test ("ushers" matching "she"/"he"/
// "hers") is removed because that behavior is exactly what the fix eliminates.

TEST_CASE("AhoCorasick: boundary - term not matched inside longer word", "[ac][boundary]") {
    // "ant" must not match inside "tenant", "want", "grant".
    // This was the original false-positive that motivated the boundary fix.
    const std::vector<StatuteRoute> routes = { make_route("r0", {"ant"}) };
    AhoCorasick ac(routes);
    REQUIRE(ac.search("the tenant wants a grant").empty());
    // But standalone "ant" (e.g. ant infestation) must still match.
    auto hits = ac.search("ant infestation found");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "ant"));
}

TEST_CASE("AhoCorasick: boundary - known false-positive class (substr inside word)", "[ac][boundary]") {
    // Canonical examples from the audit that triggered this fix.
    struct Case { std::string term; std::string bad_query; std::string good_query; };
    std::vector<Case> cases = {
        {"meth",   "the best method is to document the damage", "meth contamination found"},
        {"appeal", "i find this situation unappealing",         "i want to appeal the decision"},
        {"bond",   "the contractor is bonded and insured",      "bond not lodged by landlord"},
        {"viewing","we are reviewing our options",              "viewing scheduled for tuesday"},
        {"rat",    "i am frustrated with my landlord",         "rat infestation in the kitchen"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.term, c.bad_query, c.good_query);
        const std::vector<StatuteRoute> routes = { make_route("r0", {c.term}) };
        AhoCorasick ac(routes);
        // Must NOT fire inside an unrelated word
        REQUIRE(ac.search(c.bad_query).empty());
        // Must still fire as a standalone token
        auto hits = ac.search(c.good_query);
        REQUIRE(hit_for(hits, 0, AcField::Legacy, c.term));
    }
}

TEST_CASE("AhoCorasick: boundary - multi-word phrase still matches", "[ac][boundary]") {
    // Phrases with internal spaces are boundary-safe because the space is always
    // a non-word character. Verify they are not broken by the boundary check.
    const std::vector<StatuteRoute> routes = {
        make_route("r0", {"heat pump", "bond refund", "pet permission"})
    };
    AhoCorasick ac(routes);
    auto hits = ac.search("heat pump not working and bond refund denied");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "heat pump"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "bond refund"));
    REQUIRE_FALSE(hit_for(hits, 0, AcField::Legacy, "pet permission"));
}

TEST_CASE("AhoCorasick: boundary - term at start and end of string", "[ac][boundary]") {
    // Boundary check must handle the string edges (no char before / after).
    const std::vector<StatuteRoute> routes = { make_route("r0", {"bond", "mould"}) };
    AhoCorasick ac(routes);
    // Term at start of string (no left character)
    auto h1 = ac.search("bond not lodged");
    REQUIRE(hit_for(h1, 0, AcField::Legacy, "bond"));
    // Term at end of string (no right character)
    auto h2 = ac.search("the property has mould");
    REQUIRE(hit_for(h2, 0, AcField::Legacy, "mould"));
    // Single-word query exactly equal to the term
    auto h3 = ac.search("bond");
    REQUIRE(hit_for(h3, 0, AcField::Legacy, "bond"));
}

TEST_CASE("AhoCorasick: boundary - section references still match", "[ac][boundary]") {
    // Section refs like "s42e" contain digits and letters but appear at boundaries
    // in normalised queries (surrounded by spaces or punctuation).
    const std::vector<StatuteRoute> routes = { make_route("r0", {"s42e", "s49b"}) };
    AhoCorasick ac(routes);
    auto hits = ac.search("s42e written consent or s49b liability cap");
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "s42e"));
    REQUIRE(hit_for(hits, 0, AcField::Legacy, "s49b"));
    // Must NOT match "s42e" as a prefix of a hypothetical "s42ea"
    REQUIRE(ac.search("s42ea extended").empty());
}

TEST_CASE("AhoCorasick: boundary - non-ASCII adjacency (accepted limitation)", "[ac][boundary]") {
    // Non-ASCII bytes reset the automaton to root, so they act as boundaries.
    // "ora" in "k\xc4\x81inga ora" (kainga ora with macron): the macron bytes
    // reset the AC before "ora" is reached, then "ora" appears at a space
    // boundary and DOES match. Expected value verified empirically.
    const std::vector<StatuteRoute> routes = { make_route("r0", {"ora", "kainga"}) };
    AhoCorasick ac(routes);
    // "ora" is preceded by a space -> boundary OK -> matches
    auto h1 = ac.search("k\xc4\x81inga ora housing");
    REQUIRE(hit_for(h1, 0, AcField::Legacy, "ora"));
    // "kainga" (ASCII) does not appear in "k\xc4\x81inga" (non-ASCII 'a')
    REQUIRE_FALSE(hit_for(h1, 0, AcField::Legacy, "kainga"));
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
