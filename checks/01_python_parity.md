Check 1 - Python/C++ Route Parity Decision
==========================================

## Evidence

### Who serves NZ tenancy queries?

Production service confirmed by systemctl:
  astraea-nz-tenancy.service -> C++ binary (build-prod/apps/nz_tenancy/nz_tenancy), port 8001

Python service (astraea/):
  Serves nz_legal jurisdiction only (port 8000 via uvicorn)
  The Python nz_tenancy route table exists as a reference/dev artifact ONLY.

### Route counts

C++ nz_tenancy: 57 routes (confirmed by test "nz_tenancy: route count")
Python nz_tenancy: 27 routes
C++-only routes: 30 (not present in Python)

### Parity test scope

tests/diff/test_parity.py in Python repo tests 27 Python-defined routes against C++ decisions.
These 62/62 subset tests cover the 27 routes that exist in both repos.
The 30 C++-only routes are NOT tested by parity tests (Python has no definition for them).

### Comment update

tests/test_routing.cpp and tests/test_nz_tenancy_routes.cpp: add a note that Python
nz_tenancy routes are a dev/reference artifact; C++ has 30 additional routes not mirrored
in Python. Python parity tests cover 27/57 routes only.

## Decision

Python NZ tenancy routes are NOT authoritative for production.
C++ is the sole serving layer.
Python route parity testing is partial (27 of 57 routes) and covers only the historical
Python-defined subset. It is useful as a regression guard for those 27 routes but does not
validate the 30 C++-only routes.

Consequence: linter (lint_routes.py) runs against Python routes only. C++-only route
audit (checks/02_cpp_only_audit.txt) is a separate manual process until the linter is
extended to parse C++ route files.
