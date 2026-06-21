Check 5 - PR4 Scope Lock
========================

## What PR4 is

1. Apply stashed cleanup:
   - agreement_form: remove overly broad pet terms that cause false fires
   - flooding_uninhabitable: set priority=6 so it can dominate repairs_maintenance
     (currently both are priority=0; this also resolves the bond_agreement_sequence
     dominance issue documented in Check 3)

2. Fix C++-only route exclude_any issues from checks/02_cpp_only_audit.txt:
   - pet_permission: pest/flea/rat/mouse/mice/wasp/ant in exclude_any are SHORT_ALPHA_EXCLUDE
     (semantically intentional; add to C++ linter whitelist or extend linter to C++)
   - bond in repair_notice_s56, painter_landlord_access, property_uninhabitable_rent_abatement
     (safe because "bond" in exclude_any suppresses bond-lodging queries from non-bond routes)
   - meth/paint in accidental_damage_insurance_excess: replace bare terms with specific phrases

3. Full route-trace diff vs baseline c17055e:
   - Capture /ask/stream route_decision for all 50 eval questions at c17055e
   - Capture same after PR4 changes applied
   - Diff to identify A/B/C cases

4. Triage each diff:
   - A: bug fixed (false positive removed or missing route now fires correctly)
   - B: regression (correct route no longer fires, or wrong route now fires)
   - C: wrong-reason pass (right answer, wrong route)

5. Fix B-class regressions before eval

6. Rebaseline eval:
   - Two independent runs of eval_judge.py
   - Two-run average must be >= 67.5 (baseline 65.5 + 2.0)
   - no_violation must not regress vs baseline

## What PR4 is NOT

- PR4 does not apply stash then immediately bless a new score.
- PR4 does not skip the route-trace diff step.
- PR4 does not accept a single-run eval score.
- PR4 does not change test infrastructure (that was PR3).

## Gate conditions (all 5 checks must be closed before PR4 starts)

Check 1: 01_python_parity.md - DONE
Check 2: 02_lint_baseline.txt + 02_cpp_only_audit.txt - DONE
Check 3: 03_dominance_fixtures.txt (5 named fixtures, 22 assertions passing) - DONE
Check 4: 04_baseline_trace.md (commit c17055e, score 65.5) - DONE
Check 5: this file - DONE
