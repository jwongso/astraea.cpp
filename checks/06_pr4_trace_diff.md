Check 6 - PR4 Route Trace Diff (Stash Applied + B-fix)
=======================================================

## Comparison

Baseline: pre-stash PR3 state (commit 1f0616d)
After: PR4 stash changes with pet_permission priority=5 removed (B-fix)

## Summary

37/50 questions: no route change
13/50 questions: route decision changed

All dominance changes classified as A-class or C-class. No B-class regressions remain.

## Triage

Q3  [A] agreement_form removed from fires (pet terms removed; false positive fixed)
Q6  [A] accidental_damage_insurance_excess now fires (carpet query was gap before; bare "carpet"
        was in exclude_any, blocking the route; now using specific phrases)
Q8  [A] bond dominates (priority=5) over agreement_form for WINZ bond sequence question
Q11 [C] repairs_maintenance -> bond dominant; both are wrong for a "landlord texting at 11pm
        about rent arrears" question (quiet_enjoyment should dominate; two priority=5 routes
        tie and bond wins on position ordering). Not a regression vs baseline.
Q12 [A] healthy_homes dominates (priority=5) for cold-house-in-winter question; more specific
        than repairs_maintenance
Q13 [A] healthy_homes_room_count dominates (priority=5) for HHS room-count question
Q18 [A] flooding_uninhabitable dominates (priority=6) for Wellington flooding question;
        agreement_form was wrong dominant before
Q20 [A] flooding_uninhabitable dominates (priority=6) for Wellington flooding/HHS question
Q24 [A] lease_break_fee dominates (priority=5) for lease break question; landlord_entry was
        wrong dominant before
Q39 [A] pet_permission dominates; agreement_form removed from fires (pet terms removed;
        Q39 = "can my landlord refuse a dog with new rules" -> correct route now)
Q45 [A] quiet_enjoyment dominates (priority=5) for "property on market, viewings" question;
        wear_and_tear was wrong dominant before
Q49 [A] No dominance change (wear_and_tear still dominates); agreement_form removed from fires
        (pet terms removed from agreement_form correctly)
        B-fix applied: pet_permission had priority=5 in stash; caused it to dominate for
        this exit inspection question with incidental "have a pet" mention. Removed priority.
Q50 [A] bond dominates (priority=5) for bond/tribunal dispute question

## B-fix Applied

pet_permission had .priority = 5 in the stash. Q49 (exit inspection + pet mention) had
pet_permission dominating over wear_and_tear incorrectly. Removed the priority field.
Rationale: Q39 (the target improvement) already works without priority=5 because
agreement_form no longer fires for pet-permission queries after the pet term cleanup.

## Stash Changes Summary

agreement_form: removed ~30 pet-related include_any terms (now in pet_permission only)
bond: added 4 lodge-related triggers; priority=5
healthy_homes: priority=5
healthy_homes_facilities: priority=5
quiet_enjoyment: priority=5
healthy_homes_room_count: priority=5
lease_break_fee: priority=5
repairs_landlord_not_fixing: priority REMOVED (was priority=5)
pet_permission: exclude_any short terms replaced with specific phrases (SHORT_ALPHA fix)
flooding_uninhabitable: priority=6
accidental_damage_insurance_excess: exclude_any meth/paint replaced with specific phrases
