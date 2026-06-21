Post-Boundary-Matcher Baseline
================================

Name:    baseline_post_boundary_matcher_b70507a
Commit:  b70507a (fix), 5cb1034 (checks)
Branch:  pr3/routes-linter
Date:    2026-06-21

Scores:
  OVERALL:              67.3 / 100
  must_include:         68.7 / 100
  no_violation:         84.4 / 100
  section_recall:       41.5 / 100
  section_faithfulness: 63.5 / 100
  accuracy:             66.1 / 100

Eval log: /home/wdha/proj/priv/astraea/.training/eval_b70507a.log
Two-run confirmed: runs 3 and 4 both = 67.3 (deterministic at temp=0)

Previous baseline: c17055e, OVERALL 65.5 (eval_c17055e.log)
Delta: +1.8 overall, +1.1 no_violation

Incompatibility note
---------------------
This baseline is NOT directly comparable to the rulecard series (rc1-rc15c, max 74.5).
The rulecard series was evaluated on a different branch state with:
  - No word-boundary AhoCorasick enforcement (PR1 not applied)
  - Different route term coverage
  - Possibly different eval protocol versions

Do not use rulecard scores to set targets for this series.
Set new targets from this baseline:
  Next target: overall 69+ (from PR5+PR6 combined)
  section_recall is the weakest dimension (41.5); targeted section-forcing is the lever.
  no_violation is healthy (84.4); do not sacrifice it for other gains.

Commit chain:
  c17055e  pre-PR1 baseline
  980c200  PR1: AhoCorasick word-boundary enforcement
  611322c  PR2a: inflected forms + appeal replacements
  623e60a  PR2b: landlord_entry plural forms
  40da247  PR3: lint_routes.py CI linter
  f9e25a3  pre-PR4 gate checks + designated-initializer field order fix
  1f0616d  PR3 CI: linter step in parity job
  25b7cef  PR4: priority weights, agreement_form cleanup, term precision (B-fix: pet_permission)
  b70507a  PR4+: restore pet_bond phrase coverage for "bond due to having pets" pattern
  5cb1034  checks: PR4 summary and eval run1 inspection
