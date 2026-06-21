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
    REQUIRE(get_routes().size() == 57);
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

TEST_CASE("nz_tenancy route: owner_occupation_notice", "[nz_tenancy][routing]") {
    auto d = decide("landlord gave me 42 days notice as she wants to move back in and is claiming i damaged her home");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "owner_occupation_notice") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: key_return_pm_closed", "[nz_tenancy][routing]") {
    auto d = decide("dropping keys off tuesday because easter monday pm office is closed and they are charging 2 extra days rent");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "key_return_pm_closed") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: meth_contamination_defence", "[nz_tenancy][routing]") {
    auto d = decide("spoken to both testing companies and they say readings differ by location i need tips for the hearing about the pre-existing contamination");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "meth_contamination_defence") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: property_sale_viewings", "[nz_tenancy][routing]") {
    auto d = decide("the realtor selling the house wants entry to show prospective buyers do i have to allow this");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "property_sale_viewings") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: listing_photos_misleading", "[nz_tenancy][routing]") {
    auto d = decide("is there any law about how old rental listing photos can be the difference between the photos and the actual house is wild");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "listing_photos_misleading") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: tribunal_application_timing", "[nz_tenancy][routing]") {
    auto d = decide("do we go to tenancy services before we leave or can we lodge it after the term is up");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "tribunal_application_timing") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: tribunal_mediation_enforcement", "[nz_tenancy][routing]") {
    auto d = decide("i sent the response from bruce to the agency but they are still issuing wrong 14 day notices ignoring our mediation agreement");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "tribunal_mediation_enforcement") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy: s49A suppressed for non-meth query", "[nz_tenancy][routing]") {
    const auto& lps = get_low_priority_sections();
    // s49A must NOT appear for a black mould / repair query
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s49A",
        "landlord giving 42 days notice wants to move back in found black mould", lps));
    // s49A MUST appear for a meth contamination query
    REQUIRE(allow_section("NZLEG/RTA/s49A",
        "landlord wants to test for meth contamination after previous tenant", lps));
    // s49A MUST appear for a contamination-defence hearing query (no explicit "meth")
    REQUIRE(allow_section("NZLEG/RTA/s49A",
        "tips for the hearing about the pre-existing contamination reading", lps));
}

TEST_CASE("nz_tenancy: LP gates are boundary-aware (PR1 parity regression)",
          "[nz_tenancy][regression]") {
    const auto& lps = get_low_priority_sections();
    // "method" must NOT open s49A - pre-fix this returned true via raw substring
    // ("meth" is a literal prefix of "method"). The boundary check rejects it.
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s49A",
        "what is the best method to document accidental damage", lps));
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s49A",
        "describe the methodology the landlord must follow", lps));
    // "pharmacy", "charm", "harmless" must NOT open s55AA.
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s55AA",
        "i went to the pharmacy to collect my prescription", lps));
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s55AA",
        "the building has charm and harmless eccentricities", lps));
    // Sanity: the actual vocabulary still opens the gates.
    REQUIRE(allow_section("NZLEG/RTA/s49A",
        "meth contamination test", lps));
    REQUIRE(allow_section("NZLEG/RTA/s55AA",
        "my ex partner physically assaulted me at the property", lps));
}

TEST_CASE("nz_tenancy: s55AA opens for common violence inflections",
          "[nz_tenancy][regression]") {
    const auto& lps = get_low_priority_sections();
    // Each of these forms appears in real tenancy questions about violence and
    // must keep s55AA visible. Pinned because the s55AA term list was extended
    // in the same PR that switched leg_allow_list to union semantics; a future
    // edit that drops one of these forms will fail this test.
    for (const auto* q : {
        "he caused an injury to my hand during the argument",
        "the landlord has injuries from the altercation",
        "i needed to injure my way out of the lock",
        "received threats over the bond dispute",
        "ongoing threatening messages from the landlord",
        "the ex partner behaved violently inside the unit",
    }) {
        INFO("query: " << q);
        REQUIRE(allow_section("NZLEG/RTA/s55AA", q, lps));
    }
}

TEST_CASE("nz_tenancy: s109 suppressed without timing vocabulary", "[nz_tenancy][routing]") {
    const auto& lps = get_low_priority_sections();
    // s109 must NOT appear for a generic repair query
    REQUIRE_FALSE(allow_section("NZLEG/RTA/s109",
        "landlord has not fixed the hot water for 8 weeks", lps));
    // s109 MUST appear when query asks about filing after leaving
    REQUIRE(allow_section("NZLEG/RTA/s109",
        "do we go to tenancy services before we leave or can we lodge it after", lps));
}

TEST_CASE("nz_tenancy route: bond_agreement_sequence", "[nz_tenancy][routing]") {
    auto d = decide("i went to winz but they need the agreement before giving me a pre approval letter");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "bond_agreement_sequence") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy route: accidental_damage_insurance_excess", "[nz_tenancy][routing]") {
    auto d = decide("my glass jar fell off the stove and cracked the glass and now pm says i am liable for the replacement");
    REQUIRE(d.triggered);
    REQUIRE(std::find(d.matched_intents.begin(), d.matched_intents.end(),
                      "accidental_damage_insurance_excess") != d.matched_intents.end());
}

TEST_CASE("nz_tenancy: s49B passes without meth vocabulary (not in LPS)", "[nz_tenancy]") {
    const auto& lps = get_low_priority_sections();
    // s49B is NOT in LPS - it should pass for any damage query
    REQUIRE(allow_section("NZLEG/RTA/s49B",
        "my glass jar cracked the stovetop and pm says i am liable for full replacement", lps));
    REQUIRE(allow_section("NZLEG/RTA/s49B",
        "accidental damage to the carpet", lps));
}

// ---------------------------------------------------------------------------
// Eval regression tests - freeze routing for 8 known eval questions
// These catch route overcapture before the 50-question eval is needed.
// ---------------------------------------------------------------------------

namespace {
bool fires(const RouteDecision& d, const std::string& intent) {
    return std::find(d.matched_intents.begin(), d.matched_intents.end(), intent)
           != d.matched_intents.end();
}
bool forces(const RouteDecision& d, const std::string& section) {
    return std::find(d.forced_sections.begin(), d.forced_sections.end(), section)
           != d.forced_sections.end();
}
} // namespace

// Q01 - signed bond release form / get bond money back
TEST_CASE("eval-Q01: exit_inspection_bond_process fires for bond refund question", "[nz_tenancy][regression]") {
    auto d = decide("how can i get my bond money back we have both moved out since last year and the landlord has not returned it");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "exit_inspection_bond_process"));
    REQUIRE(forces(d, "NZLEG/RTA/s22"));
}

// Q18 - flooding / uninhabitable / rent reduction
TEST_CASE("eval-Q18: property_uninhabitable_rent_abatement fires for flooding rent reduction", "[nz_tenancy][regression]") {
    auto d = decide("anyone in wellington who went through the flooding get a rental reduction if your property was destroyed but the house itself is still livable we are still paying rent");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "property_uninhabitable_rent_abatement"));
}

// Q19 - exit inspection wait / bond process
TEST_CASE("eval-Q19: exit_inspection_bond_process fires for exit inspection delay", "[nz_tenancy][regression]") {
    auto d = decide("what is a reasonable time to wait before pm comes back about exit inspection we handed in our keys yesterday and still waiting to hear back");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "exit_inspection_bond_process"));
    REQUIRE_FALSE(fires(d, "repairs_maintenance"));
}

// Q22 - landlord not providing reference for new rental
TEST_CASE("eval-Q22: landlord_unresponsive_reference fires for reference request", "[nz_tenancy][regression]") {
    auto d = decide("i have been short-listed for another rental but they are trying to get a reference from my landlord and the landlord wont give reference");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "landlord_unresponsive_reference"));
}

// Q36 - tribunal mediation agreement / payment date ignored by new PM
// Key conflict: "change payment date" must NOT pull this into agreement_form
TEST_CASE("eval-Q36: tribunal_mediation_enforcement fires; agreement_form suppressed", "[nz_tenancy][regression]") {
    auto d = decide("i sent my response from bruce to the agency they said you want to change payment date from saturday to tuesday we had mediation with tribunal back in september and agreed tuesday due date");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "tribunal_mediation_enforcement"));
    REQUIRE_FALSE(fires(d, "agreement_form"));
}

// Q38 - text message as valid written notice
TEST_CASE("eval-Q38: electronic_notice_s13c fires for text message notice", "[nz_tenancy][regression]") {
    auto d = decide("my tenancy ended and the landlord is asking did we give 21 days notice i texted the landlord back in november with clear notice and they acknowledged it");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "electronic_notice_s13c"));
    REQUIRE(forces(d, "NZLEG/RTA/s136"));
    REQUIRE(forces(d, "NZLEG/RTA/s13C"));
}

// Q43 - neighbour cooking meth / contamination
// Key conflict: must NOT fall through to wear_and_tear
TEST_CASE("eval-Q43: neighbour_contamination fires; wear_and_tear suppressed", "[nz_tenancy][regression]") {
    auto d = decide("i had it confirmed that they were cooking meth next door what does this mean for our health and the house i suspect there is contamination from the neighbour");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "neighbour_contamination"));
    REQUIRE_FALSE(fires(d, "wear_and_tear"));
    REQUIRE(forces(d, "NZLEG/RTA/s45"));
    REQUIRE(forces(d, "NZLEG/RTA/s40"));
}

// Q48 - broken oven / repair path
TEST_CASE("eval-Q48: repairs_maintenance fires for unrectified oven defect", "[nz_tenancy][regression]") {
    auto d = decide("i moved in november and noticed the oven wasnt closing properly i reported it at the inspection in december and the landlord still has not fixed it");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "repairs_maintenance"));
    REQUIRE(forces(d, "NZLEG/RTA/s45"));
}

// ---------------------------------------------------------------------------
// Dominance and forced-section fixtures (pre-PR4 Check 3)
// Verify that dominant_route and forced_sections are correct when multiple
// high-priority routes co-fire on realistic queries.
// ---------------------------------------------------------------------------

namespace {
bool dominates(const RouteDecision& d, const std::string& intent) {
    return d.dominant_route == intent;
}
} // namespace

// guest_damage_liability (priority=12) vs repairs_tenant_not_at_fault (priority=8)
// Both have leg_allow_list; guest_damage_liability must win on priority.
TEST_CASE("fixture: guest_damage_liability dominates repairs_tenant_not_at_fault", "[nz_tenancy][dominance]") {
    auto d = decide("my guest damaged the property and the landlord wants me to pay for the repair");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "guest_damage_liability"));
    REQUIRE(fires(d, "repairs_tenant_not_at_fault"));
    REQUIRE(dominates(d, "guest_damage_liability"));
    REQUIRE_FALSE(dominates(d, "repairs_tenant_not_at_fault"));
    REQUIRE(forces(d, "NZLEG/RTA/s40"));
    REQUIRE(forces(d, "NZLEG/RTA/s18"));
    REQUIRE(forces(d, "NZLEG/RTA/s19"));
}

// bond_agreement_sequence fires for WINZ query; repairs_maintenance does not fire.
// Note: bond_agreement_sequence has no leg_allow_list so agreement_form/bond dominate
// on this query (both have leg_allow_list at priority=0). PR4 will add a leg_allow_list
// to bond_agreement_sequence. The key assertion here is must_not_dominate repairs_maintenance.
TEST_CASE("fixture: bond_agreement_sequence fires on WINZ query; repairs_maintenance does not fire", "[nz_tenancy][dominance]") {
    auto d = decide("i need to apply to winz for bond but the property manager will not provide the tenancy agreement");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "bond_agreement_sequence"));
    REQUIRE_FALSE(fires(d, "repairs_maintenance"));
    REQUIRE_FALSE(dominates(d, "repairs_maintenance"));
}

// flooding_uninhabitable forces s55 + s45 when the main premises are flooded.
// repairs_maintenance also co-fires (both have the flood trigger); both forced
// sections are unioned. Primary check is that s55 is present.
TEST_CASE("fixture: flooding_uninhabitable forces s55 for flooded outbuildings query", "[nz_tenancy][dominance]") {
    auto d = decide("my house flooded and parts of the property are unusable the outbuildings we cannot use are part of the tenancy");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "flooding_uninhabitable"));
    REQUIRE(forces(d, "NZLEG/RTA/s55"));
    REQUIRE(forces(d, "NZLEG/RTA/s45"));
}

// pet_permission fires for a straightforward dog-permission query.
TEST_CASE("fixture: pet_permission fires for dog permission request", "[nz_tenancy][dominance]") {
    auto d = decide("can i get a dog is my landlord allowed to refuse my request for a pet");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "pet_permission"));
    REQUIRE(forces(d, "NZLEG/RTA/s42E"));
}

// pet_permission must NOT fire when the query is about a pest infestation.
// "ant" is in pet_permission exclude_any; repairs_maintenance must fire instead.
TEST_CASE("fixture: pet_permission suppressed for pest infestation context", "[nz_tenancy][dominance]") {
    auto d = decide("there is an ant infestation in the property who is responsible for dealing with it");
    REQUIRE(d.triggered);
    REQUIRE_FALSE(fires(d, "pet_permission"));
    REQUIRE(fires(d, "repairs_maintenance"));
}

// Q29 fixture: guest damage + landlord demand for extra bond due to pets.
// Both guest_damage_liability (s40) and pet_bond (s18AA) must fire so the model
// has the evidence to correctly say: tenant IS liable for guest damage AND
// landlord CANNOT demand an extra bond retroactively for existing pets.
// Regression: PR1 word-boundary fix caused pet_bond to stop matching "extra
// weeks bond due to having pets" because the old broad include_any terms
// relied on substring overlap. Added specific multi-word phrases to restore.
TEST_CASE("fixture Q29: guest damage + extra pet bond demand forces s40 and s18AA", "[nz_tenancy][dominance][regression]") {
    auto d = decide("we had a relative house sitting who caused damage to the walls and the landlord wants two extra weeks bond due to having pets even though they knew we had pets for two years");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "guest_damage_liability"));
    REQUIRE(fires(d, "pet_bond"));
    REQUIRE(forces(d, "NZLEG/RTA/s40"));
    REQUIRE(forces(d, "NZLEG/RTA/s18AA"));
    REQUIRE_FALSE(dominates(d, "pet_bond"));
}

// Q9 fixture: text-message notice validity + periodic tenancy.
// electronic_notice_s13c must fire and s13C must be forced even when
// termination_notice also fires. Under UNION leg_allow_list semantics
// both routes' allow-lists are surfaced; the historical workaround of
// hand-adding s13C/s136 into termination_notice.leg_allow_list is now
// strictly redundant (kept for now; safe to clean up in a follow-up).
TEST_CASE("fixture Q9: electronic text notice forces s13C alongside termination sections", "[nz_tenancy][dominance][regression]") {
    auto d = decide("my tenancy ended in february i texted the landlord back in november to give notice the landlord is now asking did you give 21 days notice is a text message valid notice");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "electronic_notice_s13c"));
    REQUIRE(forces(d, "NZLEG/RTA/s13C"));
    REQUIRE(forces(d, "NZLEG/RTA/s136"));
    REQUIRE(forces(d, "NZLEG/RTA/s51"));
}

// Q4 fixture: landlord's neighbour enters with gate remote.
// landlord_entry must fire and force both s48 and s38. The dominant route must
// be landlord_entry, not a repairs or other route. Terms "unauthorised person"
// and "gate remote" were added to the include_any list for this pattern.
TEST_CASE("fixture Q4: landlord neighbour gate remote entry forces s48 and s38", "[nz_tenancy][dominance][regression]") {
    auto d = decide("our landlord is overseas and his neighbour has a gate remote and just lets himself in to start the landlords cars my teenage daughter has been home alone is he allowed to do this");
    REQUIRE(d.triggered);
    REQUIRE(fires(d, "landlord_entry"));
    REQUIRE(forces(d, "NZLEG/RTA/s48"));
    REQUIRE(forces(d, "NZLEG/RTA/s38"));
    REQUIRE(dominates(d, "landlord_entry"));
}
