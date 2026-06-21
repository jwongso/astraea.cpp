Check 7 - Eval Run 1 Results (PR4)
===================================

## Run details

Commit: 25b7cef
Log: /home/wdha/proj/priv/astraea/.training/eval_pr4_run1.log
Date: 2026-06-21

## Scores

  must_include:         68.7 / 100  (baseline 66.3)
  no_violation:         83.0 / 100  (baseline 83.3)  <- no regression
  section_recall:       41.0 / 100  (baseline 39.0)
  section_faithfulness: 63.0 / 100  (baseline 62.5)
  accuracy:             65.2 / 100  (baseline 64.7)
  OVERALL:              66.7 / 100  (baseline 65.5)  delta=+1.2

## Inspection checklist

### no_violation
83.0 vs 83.3 baseline. Delta = -0.3. Within run-to-run variance. No regression.

### Bottom 10
  2799281706908774  39.1  (no change vs baseline)
  2773174209519524  39.5  (no change vs baseline)
  2805576776279267  45.0  (delta -3.1, minor)
  2881508202019457  45.6  (delta +14.0, improvement)
  2868510296652581  45.6  (delta +4.5, improvement)
  2871951879641756  45.6  (delta -3.1, minor)
  2658766607626952  48.7  (delta -1.0, minor)
  2892326194270991  49.1  (no change)
  2789705801199698  51.5  (no change)
  2865632593607018  53.7  (delta -25.5, see below)

Persistent low scorers (unchanged, chronically hard) are Q14/Q37/Q10 class questions.

### >15 point drops

Only one: Q29 (2865632593607018) - base=79.2, PR4=53.7, delta=-25.5

Classification: D-class (answer/judge variance, not routing regression)
Evidence:
  - Q29 is in the "37 unchanged" bucket of trace diff (c17055e vs 25b7cef)
  - Route decision identical in baseline and PR4
  - s45 force from repairs_maintenance (question mentions "repairs") - same in both runs
  - faith dropped 75->25 but rec improved 50->75 (more sections cited, judge found them unfaithful)
  - Complex 3-issue question (guest damage + pet bond + pre-existing defects)
  - Pattern matches judge threshold variance on multi-issue answers

Action: none required. Proceed to run 2 and average.

### Major gains
  2713300475506898  +45.2  Q39 (pet_permission, dog question) - A-class
  2825745190929092  +16.6  Q12 (healthy_homes) - A-class
  2881508202019457  +14.0  Q20 (flooding_uninhabitable) - A-class

### flooding_uninhabitable overcapture
No ordinary repair questions gained flooding_uninhabitable route in trace diff.
Only Q18 and Q20 (both genuine flooding/uninhabitable scenarios) gained it.

### agreement_form cleanup impact
Q3: agreement_form correctly removed from fires (pet term false positive fixed)
Q39: pet_permission now dominates (agreement_form was blocking; score +45.2)
Q49: wear_and_tear still dominates (B-fix worked; no regression)


## Q29 Root Cause Analysis

Q29 (2865632593607018): "guest damage + landlord demands extra bond due to having pets"

Root cause: PR1 word-boundary AhoCorasick fix caused pet_bond to stop matching
"extra weeks bond due to having pets" because include_any terms like "extra bond for pet"
required exact adjacency that did not exist in the natural query phrasing.

Effect: pet_bond did not fire -> s18AA not retrieved -> model lost pet bond retroactivity
context -> model contradicted s40 by incorrectly stating tenant is not liable for guest damage.

Classification reclassified: B/D hybrid (B: answer regression causing wrong legal advice;
root: PR1-side effect on pet_bond coverage, not PR4 change)

Fix (commit b70507a):
- Added multi-word phrases to pet_bond include_any:
  "bond due to having pets", "bond due to having a pet",
  "bond because of pets", "extra bond due to having pet", etc.
- Added regression fixture test "fixture Q29: guest damage + extra pet bond demand forces s40 and s18AA"
- All 189 tests pass; parity 62/62; linter 0 findings

Eval runs 1 and 2 (66.7 each) are INVALIDATED - they were scored before the Q29 fix.
Run 3 (in progress) is the new run 1 for the PR4 acceptance pair.
