#include "nz_tenancy/routes.hpp"
#include "astraea/route_table.hpp"
#include "astraea/routing.hpp"
#include <catch2/catch_all.hpp>
#include <algorithm>

using namespace astraea;
using namespace astraea::nz_tenancy;

// ---------------------------------------------------------------------------
// Table sanity
// ---------------------------------------------------------------------------

TEST_CASE("nz_tenancy: route count", "[nz_tenancy]") {
    REQUIRE(get_routes().size() == 48);
}

TEST_CASE("nz_tenancy: all intents are unique", "[nz_tenancy]") {
    std::vector<std::string> intents;
    for (const auto& r : get_routes()) intents.push_back(r.intent);
    std::sort(intents.begin(), intents.end());
    REQUIRE(std::adjacent_find(intents.begin(), intents.end()) == intents.end());
}

TEST_CASE("nz_tenancy: two-tier routes have no include_any", "[nz_tenancy]") {
    for (const auto& r : get_routes()) {
        if (!r.include_any_precise.empty() || !r.include_any_broad.empty())
            REQUIRE(r.include_any.empty());
    }
}

TEST_CASE("nz_tenancy: low priority sections contain expected IDs", "[nz_tenancy]") {
    const auto& lps = get_low_priority_sections();
    REQUIRE(std::any_of(lps.begin(), lps.end(),
        [](const auto& p) { return p.first == "NZLEG/RTA/s16A"; }));
    REQUIRE(std::any_of(lps.begin(), lps.end(),
        [](const auto& p) { return p.first == "NZLEG/RTA/s55AA"; }));
}

// ---------------------------------------------------------------------------
// Routing smoke tests - representative query per route group
// ---------------------------------------------------------------------------

static RouteDecision decide(const std::string& q) {
    // Static RouteTable shared by every decide() call - the AC is built once
    // per test process instead of per call. Same pattern a production
    // Jurisdiction should adopt at startup.
    static const RouteTable table{get_routes()};
    return build_route_decision(q, "", table);
}

TEST_CASE("nz_tenancy route: wear_and_tear", "[nz_tenancy][routing]") {
    auto d = decide("the landlord is deducting from my bond for carpet wear");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "wear_and_tear") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: property_change precise", "[nz_tenancy][routing]") {
    auto d = decide("can i put up a security camera without landlord consent");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "property_change") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: repairs_maintenance", "[nz_tenancy][routing]") {
    auto d = decide("the heat pump is not working and landlord won't fix it");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "repairs_maintenance") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: bond", "[nz_tenancy][routing]") {
    auto d = decide("the landlord has not lodged my bond");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "bond") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: landlord_entry", "[nz_tenancy][routing]") {
    auto d = decide("the landlord came into my home without notice");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "landlord_entry") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: termination_notice", "[nz_tenancy][routing]") {
    auto d = decide("i received a 90 day notice to vacate");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "termination_notice") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: fixed_term_rent_review (include_all)", "[nz_tenancy][routing]") {
    auto d = decide("there is a rent review clause in my fixed term agreement");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "fixed_term_rent_review") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: fixed_term_rent_review does not fire without 'fixed term'", "[nz_tenancy][routing]") {
    auto d = decide("there is a rent review clause in my agreement");
    // rent_increase may fire but fixed_term_rent_review must not
    bool ftr_fired = std::find(d.matched_intents.begin(), d.matched_intents.end(),
                               "fixed_term_rent_review") != d.matched_intents.end();
    REQUIRE_FALSE(ftr_fired);
}

TEST_CASE("nz_tenancy route: healthy_homes", "[nz_tenancy][routing]") {
    auto d = decide("the landlord has not met healthy homes standards for insulation");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "healthy_homes") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: rent_arrears", "[nz_tenancy][routing]") {
    auto d = decide("i received a 14 day notice for rent arrears");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "rent_arrears") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: family_violence_exit", "[nz_tenancy][routing]") {
    auto d = decide("i have a protection order and need to leave the tenancy");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "family_violence_exit") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: property_change exclude suppresses on landlord entry vocabulary", "[nz_tenancy][routing]") {
    // "entered my home" is in property_change exclude_any - should not fire that route
    auto d = decide("the homeowner entered my home without permission");
    bool pc_fired = std::find(d.matched_intents.begin(), d.matched_intents.end(),
                              "property_change") != d.matched_intents.end();
    REQUIRE_FALSE(pc_fired);
    // landlord_entry should catch it instead
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "landlord_entry") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: carpark_dispute priority and allow_list", "[nz_tenancy][routing]") {
    auto d = decide("the landlord is removing my carpark from the tenancy agreement");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "carpark_dispute") != d.matched_intents.end());
    // carpark_dispute has priority=8 and defines leg_allow_list; it must dominate
    // and its allow_list must be surfaced (not a union from other matched routes).
    REQUIRE(d.dominant_route == "carpark_dispute");
    REQUIRE_FALSE(d.leg_allow_list.empty());
}

TEST_CASE("nz_tenancy: allow_section gates s16A and s55AA", "[nz_tenancy]") {
    const auto& lps = get_low_priority_sections();
    // s16A is suppressed unless overseas vocabulary is present
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s16A", "landlord failed to fix the heating", lps));
    REQUIRE(allow_section("NZLEG/RTA/s16A", "my overseas landlord has not appointed an agent", lps));
    // s55AA suppressed without violence vocabulary
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s55AA", "landlord wants to terminate my tenancy", lps));
    REQUIRE(allow_section("NZLEG/RTA/s55AA", "tenant physically assaulted the landlord", lps));
    // unknown section always passes
    REQUIRE(allow_section("NZLEG/RTA/s99", "anything at all", lps));
}

TEST_CASE("nz_tenancy route: subletting_without_consent", "[nz_tenancy][routing]") {
    auto d = decide("can i sublet the property without telling my landlord");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "subletting_without_consent") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: tribunal_repair_order", "[nz_tenancy][routing]") {
    auto d = decide("can the tribunal issue a work order forcing the landlord to fix the roof");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "tribunal_repair_order") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: tribunal_post_filing", "[nz_tenancy][routing]") {
    auto d = decide("is it appropriate for a property manager to communicate with you after filing for tribunal");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "tribunal_post_filing") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: guest_damage_liability", "[nz_tenancy][routing]") {
    auto d = decide("our relative caused damage while house sitting and landlord wants extra bond");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "guest_damage_liability") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: neighbour_contamination", "[nz_tenancy][routing]") {
    auto d = decide("our neighbours were cooking meth and chemical smell wafting into our house");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "neighbour_contamination") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: landlord_unresponsive_reference", "[nz_tenancy][routing]") {
    auto d = decide("i need a rental reference from my landlord but they won t respond");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "landlord_unresponsive_reference") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: post_inspection_followup", "[nz_tenancy][routing]") {
    auto d = decide("got a call after inspection that i would receive a letter but i have not received a letter and remedied the issue do they inspect the whole property on reinspection");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "post_inspection_followup") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: flooding_uninhabitable", "[nz_tenancy][routing]") {
    auto d = decide("went through the flooding and the outbuildings we cannot use are part of the rooms in the tenancy agreement can i get a rental reduction");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "flooding_uninhabitable") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: moveout_rent_calculation", "[nz_tenancy][routing]") {
    auto d = decide("i am moving out on a monday and only owe 3 days rent for saturday sunday and monday how do i calculate that");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "moveout_rent_calculation") != d.matched_intents.end());
}
