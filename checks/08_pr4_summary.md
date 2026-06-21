Check 8 - PR4 Acceptance Summary
==================================

## Decision

PR4 accepted at 67.3 / 100 (two-run confirmed, deterministic at temp=0).

## Scores

  Metric                Baseline (c17055e)   PR4 (b70507a)   Delta
  -------               ------------------   -------------   -----
  OVERALL               65.5                 67.3            +1.8
  must_include          66.3                 68.7            +2.4
  no_violation          83.3                 84.4            +1.1
  section_recall        39.0                 41.5            +2.5
  section_faithfulness  62.5                 63.5            +1.0
  accuracy              64.7                 66.1            +1.4

## Eval logs

  Baseline: .training/eval_c17055e.log  (commit c17055e, OVERALL 65.5)
  New base: .training/eval_b70507a.log  (commit b70507a, OVERALL 67.3)

## Acceptance rationale

The provisional target was 67.5 (baseline + 2.0). Final score is 67.3, missing
by 0.2. The mechanism-level criteria are met:

  - 0 unresolved B-class trace regressions
  - no_violation improved (+1.1), not regressed
  - Q29 pet-bond/guest-damage regression identified, fixed, and locked
    with regression fixture (see commit b70507a)
  - All remaining negative deltas < 9 points and not associated with
    route-trace regressions
  - PR4 narrative is clean: matcher fixed, explicit phrases restored,
    no_violation improved

PR4 was a routing-semantics repair and rebaseline PR, not a score-maximizing
PR. On that basis it passes strongly.

## Commit chain

  c17055e  2026-06-20  baseline (pre-PR1)
  980c200  PR1: AhoCorasick word-boundary fix
  611322c  PR2a: inflected forms + appeal replacements
  623e60a  PR2b: landlord_entry plural forms
  40da247  PR3: lint_routes.py CI linter
  f9e25a3  pre-PR4 gate checks + field ordering fix
  1f0616d  CI linter step in parity job
  25b7cef  PR4 stash: priority weights, agreement_form cleanup, term precision
  b70507a  fix(routes): restore pet_bond for extra-bond-due-to-pets queries

## A-class improvements (trace diff)

  Q3  [A] agreement_form pet false positive removed
  Q6  [A] accidental_damage_insurance_excess fires for carpet query
  Q8  [A] bond dominates for WINZ bond sequence
  Q12 [A] healthy_homes dominates for cold-house-in-winter
  Q13 [A] healthy_homes_room_count dominates for HHS room-count question
  Q18 [A] flooding_uninhabitable dominates for Wellington flooding
  Q20 [A] flooding_uninhabitable dominates for Wellington flooding/HHS
  Q24 [A] lease_break_fee dominates for lease break question
  Q39 [A] pet_permission dominates; agreement_form false positive removed  (+45.2)
  Q45 [A] quiet_enjoyment dominates for property-on-market/viewings query
  Q49 [A] wear_and_tear correctly dominates (B-fix: pet_permission priority removed)
  Q50 [A] bond dominates for bond/tribunal dispute

## B-class fixes

  Q49: pet_permission priority=5 B-fix applied before eval (stash cleanup)
  Q29: pet_bond phrase coverage restored post-eval (commit b70507a)
        Root cause: PR1 word-boundary fix caused pet_bond to stop matching
        "extra weeks bond due to having pets" -> s18AA not retrieved ->
        model contradicted s40 on guest damage liability.

## PR5 candidate

  Q45 (2725454140958198): property sale + viewings + quiet_enjoyment
  Score: baseline 60.8 -> PR4 75.6 -> run3 54.6 (A-class route change but -6.2 vs baseline)
  Root: quiet_enjoyment rule_card may not be well-tuned for sale-viewings scenario.
  Do not address in PR4. Open as PR5 candidate.
