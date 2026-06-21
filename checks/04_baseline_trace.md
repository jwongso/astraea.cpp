Check 4 - Pre-PR1 Eval Baseline Trace
======================================

## Baseline commit

Commit: c17055e
Date:   2026-06-20 22:45:28
Message: nz_tenancy: fix meth_contamination_defence - add s45, correct rule_card

## Eval result

OVERALL: 65.5/100 (50 questions, GPT-4o judge)
Log: /home/wdha/proj/priv/astraea/.training/eval_c17055e.log

This baseline predates BOTH the PR1 matcher change (word-boundary AhoCorasick)
AND the PR2 route-content additions (inflected forms, appeal replacements).

## Commit sequence (C++ repo)

c17055e  2026-06-20  [BASELINE captured here]
980c200  2026-06-21  PR1: fix(aho_corasick): enforce word boundaries on all route term hits
611322c  2026-06-21  PR2a: fix(routes): add missing inflected forms and replace polysemous appeal exclusions
623e60a  2026-06-21  PR2b: fix(routes): add plural forms for landlord_entry inspection terms
40da247  2026-06-21  PR3: feat(tools): add lint_routes.py CI linter for route term safety

## Why this matters

Any post-PR4 eval run must beat 65.5 + 2.0 = 67.5 (two-run average) to be accepted.
The baseline was locked before any of the PR1-PR4 changes were applied.
Using a pre-PR1 baseline ensures the eval measures the combined effect of all changes
rather than locking in an intermediate state.
