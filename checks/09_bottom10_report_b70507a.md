Bottom-10 Report - Post-Boundary-Matcher Baseline (b70507a)
===========================================================

Generated: 2026-06-21
Baseline: eval_b70507a.log (OVERALL 67.3, commit b70507a)

NOTE: This baseline is NOT directly comparable to the rc-series scores (72.x).
Routing semantics changed (word-boundary AhoCorasick) and eval protocol differs.
Compare only within the boundary-matcher series.

NOTE: Q11 (2842173055952972) is volatile. Excluded from actionable analysis.

---

## Bottom 10 (sorted by overall, volatile Q11 included for position)

Rank  ID                  overall  must  rec  faith  acc  leg  Classification
  1   2799281706908774     39.1    30    25    50     30   3   wrong-forced-sections + answer-reasoning
  2   2773174209519524     39.5    40    25    25     30   3   wrong-forced-sections + retrieval-gap
  3   2881508202019457     42.5    40    25    25     45   4   multi-issue + wrong-forced-sections
  4   2805576776279267     45.0    40    25    50     42   2   wrong-forced-sections
  5   2868510296652581     45.6    40    25    50     45   2   missing-practical-step
  6   2871951879641756     48.7    40    25    75     45   3   missing-practical-step
  7   2892326194270991     49.1    50    25    50     45   5   answer-reasoning-failure
  8   2658766607626952     49.7    25    75    75     45   1   missing-practical-step
  9   2789705801199698     51.5    60    25    25     55   5   wrong-forced-sections
 10   2842173055952972     54.6    60    25    50     55   4   VOLATILE - skip
 11   2725454140958198     54.6    60    50    25     55   2   PR5 candidate (Q45)

---

## Detailed Classifications

### Q37 (2799281706908774) - 39.1 - wrong-forced-sections + answer-reasoning-failure
Labels: fixed_term_early_ending, mutual_agreement, rent_liability
Retrieved: s45, s40, s13A (WRONG - should be s50, s60A, s61)
Gold: s50 (mutual agreement ends tenancy), s60A (fixed-term auto-ends), s61(3)(b) (mitigation duty)

Root: Route fired repairs/entry sections instead of tenancy termination sections.
Model made a direct legal error: said "landlord is not required to mitigate" when s61(3)(b)
imposes a mitigation duty.
Must-misses: mutual agreement binding from phone+written confirmation, rent paid to 13 March,
  no rent owed beyond that date, landlord mitigation duty.

Actionability: MEDIUM. Needs a route for fixed-term mutual-agreement scenarios or a
rule card that explicitly forces s50+s60A+s61. Interaction with existing routes is complex.
Risk: high if a new route overcaptures fixed-term queries away from correct routes.

---

### Q14 (2773174209519524) - 39.5 - wrong-forced-sections + retrieval-gap
Labels: bond_not_lodged, unlawful_termination_fixed_term, exemplary_damages
Retrieved: s45, s40, s18 (wrong)
Gold: s19 (lodgment duty), s22 (exemplary damages for non-lodgment), s18 (general bond info)

Root: Route fired maintenance sections (s45, s40) and generic bond (s18) but NOT s19
(landlord's duty to lodge bond within 23 working days) or s22 (exemplary damages).
Model confused "bond return" with "bond lodgment" - completely different legal issue.
Must-misses: 23-working-day lodgment deadline, 12-month filing window, exemplary damages.

Actionability: MEDIUM. Adding s19+s22 to bond or a new bond-not-lodged route's forced_sections
could fix it. But the fixed-term unlawful termination aspect is a second issue that also
needs coverage. Multi-issue risk.

---

### Q20 (2881508202019457) - 42.5 - multi-issue + wrong-forced-sections
Labels: flooding_damage, rent_reduction, maintenance_breach, unlawful_room, HHS
Retrieved: s13A, s13B, s138B, HHS/r28
Gold: s45 (maintenance), s59 (rent reduction/abatement)

Root: flooding_uninhabitable fires (A-class improvement) but does NOT force s59 (rent reduction).
HHS route fires alongside and forces s138B (HHS assessment), which triggers the model to
recommend a private HHS assessment - a direct no_violation hit (gold rubric explicitly says
don't pay for assessment, own evidence is sufficient).
Must-misses: s59 rent reduction for unusable shed/sleepout, HHS assessment not required.
Violation: recommends private HHS assessment.

Actionability: HIGH for the HHS assessment issue. Adding an explicit rule card note to
flooding_uninhabitable or the HHS route saying "private assessment is NOT required; tenant's
own evidence is sufficient" would fix the violation. Also add s59 to flooding_uninhabitable
forced_sections. Targeted fix, low risk.

---

### Q4 (2805576776279267) - 45.0 - wrong-forced-sections
Labels: unlawful_entry, quiet_enjoyment, unauthorised_agent, trespass_notice
Retrieved: s41, s45 (wrong)
Gold: s48 (entry rights, 24h written notice), s38 (quiet enjoyment)

Root: Route fired s41 (assignment/subletting) and s45 (maintenance), NOT s48 (landlord
entry rights) or s38 (quiet enjoyment breach). Wrong route dominant.
Must-misses: s48 right requiring 24h written notice, trespass notice option, no exception
for landlord's friend/neighbour, safety of minor at home.
Violation: implied neighbour's "friend" status may create some authority.

Actionability: HIGH. landlord_entry or quiet_enjoyment route should fire here and force
s48+s38. The unauthorised agent aspect (friend of landlord) might be why wrong route fires.
Need to check what actually dominates and add terms covering "friend of landlord", "unauthorised
person", "someone who is not the landlord".

---

### Q29 (2868510296652581) - 45.6 - missing-practical-step
Labels: tribunal_procedure_and_admissibility, evidence_credibility
Retrieved: s22B (gold - present but not cited!), s55A
Gold: s22B (Tribunal procedure)

Root: Model retrieved s22B (the right section) but DID NOT cite or use it in the answer.
Must-misses: Tribunal disregards personal attacks/immigration status, compliance letter has
no bearing on Tribunal's independent assessment, adjudicators trained to assess credibility,
structured factual rebuttal approach.
No clear route for "tribunal hearing procedure + evidence admissibility" exists.

Actionability: MEDIUM. A rule card for tribunal hearing preparation that explicitly names
what adjudicators do/don't consider would help. But this is a specialized pre-hearing scenario
not covered by existing routes. New route risk: overcapture on tribunal queries.

---

### Q21 (2871951879641756) - 48.7 - missing-practical-step
Labels: landlord_obligations_cleanliness, repair_reimbursement, breach_entry_condition
Retrieved: s45, s40, s138B (correct sections, wrong subsections used)
Gold: s45(1)(a) (premises must be reasonably clean), s45(1)(d) (reimbursement conditions)

Root: Route fires s45 correctly but model uses s45(1)(a) (cleanliness) without including
s45(1)(d) (reimbursement conditions: serious/urgent, notice given). The model also
doesn't mention: 5-working-day deadline in formal demand, witness statement leverage,
or curtain/remedial order claim.
Prison reintegration worker context = complex factual pattern with many claim types.

Actionability: LOW for PR5. Too many specific practical steps for a single rule card fix.
The s45(1)(d) detail could be added to a repairs rule card but risks cluttering the general
case. Skip for PR5.

---

### Q31 (2892326194270991) - 49.1 - answer-reasoning-failure
Labels: rent_cycle, part_week_rent, tenancy_termination
Retrieved: s51, s60A, s23, s50, s27 (5 sections - correct)
Gold: s23 (rent in advance), s27 (rent apportionment)

Root: Model retrieved correct sections but produced a contradictory calculation: first said
6 days owed then pivoted to 3 days. The contradiction is an arithmetic/reasoning failure,
not a retrieval or routing failure. Must also include written notice of move-out date.

Actionability: LOW. Rule card with explicit pro-rata calculation steps could help, but
arithmetic errors in models are hard to fix reliably via rule cards. Skip for PR5.

---

### Q10 (2658766607626952) - 49.7 - missing-practical-step
Labels: tribunal_appeal, appeal_procedure, appeal_success_likelihood, appeal_deadline
Retrieved: s117 (correct gold section, present and partially cited)
Gold: s117

Root: Model correctly found s117 but MISSED the specific details in s117:
  - 10 working day filing deadline
  - Appeal filed at District Court (not Tribunal)
Must also not imply District Court "re-hears" the matter (legal error not a re-hearing).

Actionability: HIGH. This is ideal for PR5. A rule card addition to tribunal_appeal (or
equivalent route) with the specific deadline and venue would fix must_include misses without
changing route selection at all. s117 is already retrieved - the model just needs guidance
to extract the right details.

---

### Q9 (2789705801199698) - 51.5 - wrong-forced-sections
Labels: notice_to_vacate, notice_validity, written_communication, landlord_acceptance
Retrieved: s51, s60A, s136, s48, s50 (s13C NOT retrieved - critical miss)
Gold: s13C (text message = valid written notice), s51 (tenant notice)

Root: s13C validates text messages as written notice for tenants. It was NOT retrieved,
so model had no basis to confirm the November text is valid notice. Model also failed to
correct tenant's mistaken belief that the 90-to-21 day rule applies to tenants (it doesn't).
Cited non-existent subsection s51(2A).

Actionability: HIGH. Adding s13C to a relevant route's forced_sections would fix the
retrieval gap. The route needs a term covering "text message notice", "written by text",
"email notice", "electronic notice validity". This was identified as a needed fix as far
back as the oracle eval notes.

---

## PR5 Candidate Shortlist (small + targeted)

Priority  ID                  overall  Fix type                   Est. gain
HIGH      2658766607626952     49.7   rule card: s117 details     +15 to +25
HIGH      2789705801199698     51.5   force s13C + term           +10 to +20
HIGH      2805576776279267     45.0   wrong dominant route fix    +10 to +20
MEDIUM    2881508202019457     42.5   HHS assessment note + s59   +10 to +15
MEDIUM    2773174209519524     39.5   bond s19+s22 forced sections +5 to +15
LOW       2799281706908774     39.1   new mutual-agreement route   risky

Avoid for PR5: Q37 (mutual-agreement), Q31 (arithmetic), Q21 (reintegration),
              Q29-tribunal-procedure (specialized), Q11 (volatile).

---

## Q45 (PR5 candidate from PR4 trace diff)

2725454140958198 - 54.6 - Labels: landlord_entry_viewings, quiet_enjoyment, sale_of_property
Baseline: 60.8, PR4: 54.6 (delta -6.2)
quiet_enjoyment now dominates (A-class route change) but score dipped.
Do not investigate without reviewing Q45 judge notes and answer first.
Add to "investigate before committing" list.

---

## Note: claim-level faithfulness (architecture candidate)

Section faithfulness average is 63.5 (baseline). The pattern across bottom-10:
- Model retrieves correct sections but cites wrong subsections (s51(2A) not real)
- Model retrieves section but fails to extract specific numbers (s117: 10 working days)
- Model uses section to support wrong proposition (Q29/Q37 legal inversions)

These are all downstream of retrieval success but upstream of answer quality.
A claim-level evaluator (answer -> atomic claims -> each claim supported/unsupported
by retrieved text) would catch all three patterns.
Recommended: prototype on 10-15 answers before integrating. Separate from PR5 route work.
