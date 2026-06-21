#!/usr/bin/env python3
"""
tools/lint_routes.py - CI linter for NZ tenancy route terms.

Prevents reintroducing raw-substring-style route terms after the
word-boundary matcher fix (PR1 of astraea.cpp). Each route term must
be safe under the boundary-aware matcher, semantically unambiguous in
its field, and non-duplicate.

Usage:
    python tools/lint_routes.py [--mode report|ci] [--jurisdiction nz_tenancy]
                                [--whitelist tools/lint_whitelist.json]
                                [--astraea-py /path/to/astraea]

Modes:
    report  Print all findings; exit 0 regardless of findings (default).
    ci      Print all findings; exit 1 if any hard-fail findings exist.

A finding is "hard-fail" if it would reintroduce a silent bug. Warnings
are always printed in both modes but never cause a non-zero exit.

This linter prevents reintroducing raw-substring-style route terms after
PR1 boundary matching. If the boundary check is ever removed or bypassed,
these terms would silently fire (or suppress) routes for unrelated queries.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import textwrap
from dataclasses import dataclass, field
from typing import NamedTuple

# ---------------------------------------------------------------------------
# Path setup: mirror tests/diff/conftest.py
# ---------------------------------------------------------------------------

_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
_BUILD = _REPO_ROOT / "build-prod"


def _find_astraea_py(override: str | None = None) -> pathlib.Path:
    candidates = []
    if override:
        candidates.append(pathlib.Path(override))
    env = os.environ.get("ASTRAEA_PY_PATH")
    if env:
        candidates.append(pathlib.Path(env))
    candidates.append(_REPO_ROOT.parent / "astraea")
    candidates.append(pathlib.Path.home() / "proj" / "astraea")
    for c in candidates:
        if (c / "core" / "routing.py").is_file():
            return c
    raise SystemExit(
        "ERROR: Cannot locate the Python astraea checkout.\n"
        "       Set ASTRAEA_PY_PATH or pass --astraea-py <path>."
    )


def _bootstrap(astraea_py_path: pathlib.Path) -> None:
    """Stub FastAPI and add paths so the jurisdiction module loads cleanly."""
    from unittest.mock import MagicMock

    if "fastapi" not in sys.modules:
        _fa = MagicMock()

        class _HTTPEx(Exception):
            def __init__(self, status_code: int = 400, detail=None):
                self.status_code = status_code
                self.detail = detail

        _fa.HTTPException = _HTTPEx
        sys.modules["fastapi"] = _fa
        sys.modules["fastapi.responses"] = MagicMock()
        sys.modules["fastapi.middleware"] = MagicMock()
        sys.modules["fastapi.middleware.cors"] = MagicMock()

    for p in [str(_BUILD), str(astraea_py_path)]:
        if p not in sys.path:
            sys.path.insert(0, p)


# ---------------------------------------------------------------------------
# Danger probe corpus
# ---------------------------------------------------------------------------
# Common English or NZ-tenancy words that contain short route terms as
# NON-BOUNDARY substrings. A term that appears in this list at a non-word-
# boundary position would have caused a false match under raw-substring
# matching. Even with the boundary-aware matcher in place, adding such a
# term is a risk signal: if the boundary check is ever removed or the term
# is used in a field that bypasses it, silent bugs return.
#
# Rule: "non-boundary" means the character immediately before or after the
# term match is a route word character ([a-z0-9_]).

DANGER_PROBES: list[str] = [
    # meth
    "method", "methodology", "methodical",
    # ant
    "tenant", "want", "grant", "relevant", "important", "plentiful",
    "accountant", "consultant", "stagnant",
    # rat
    "frustrated", "separate", "operates", "operated", "vibrate", "demonstrate",
    "arbitrate", "celebrate",
    # appeal
    "unappealing", "unappealed",
    # view (bare "view" would match "viewing", "reviewing", etc.)
    "reviewing", "reviewed",
    # paint (bare "paint" would match "repainting", "painted" in different context)
    "repainting", "repainted",
    # alter (bare "alter" matches "alternative", "alteration")
    "alternative", "alternatives", "alteration", "alterations",
    # bond (bare "bond" matches "bonded", "bonding")
    "bonded", "bonding",
    # fix (bare "fix" matches "fixture", "prefix", "affix")
    "fixture", "fixtures", "prefix", "affix",
    # lock (bare "lock" matches "clockwork", "flocking", "unlock", "gridlock")
    "clockwork", "flocking", "unlock", "gridlock",
    # notice (bare "notice" matches "unnoticed")
    "unnoticed",
    # land (bare "land" would match "landlord")
    "landlord", "landlords",
    # rent (bare "rent" matches "current", "parrent", "torrent")
    "current", "torrent", "different", "parent",
    # lease (bare "lease" matches "release", "unleash")
    "release", "unleash",
    # end (bare "end" matches "render", "blend", "lender", "extended")
    "render", "blender", "lender", "extended", "amendment",
    # old (bare "old" matches "cold", "told", "bold", "household")
    "household", "withheld",
    # own (bare "own" matches "town", "rown", "known", "owner")
    "known", "brown", "town", "downtown",
    # run (bare "run" matches "running", "overrun", "truncate")
    "running", "overrun", "truncate",
    # pet (bare "pet" matches "petrol", "competitive", "carpet")
    "petrol", "competitive", "carpet",
    # cat (bare "cat" matches "caterpillar", "locate", "education")
    "caterpillar", "locate", "education", "indicate",
    # dog (bare "dog" matches "dodgem", "hotdog")
    "hotdog",
    # rat included above
    # oven (bare "oven" matches "coven")
    "coven",
    # damp (bare "damp" matches "dampen", "dampness" at word level but after
    # boundary fix those are still distinct - keeping for audit signal)
    "dampening",
    # flush (bare "flush" in "blushing" = non-boundary)
    "blushing",
    # eve (if used) matches "leaves", "achieve"
    # kept minimal - extend as new terms are added
]

# Deduplicate while preserving order
_seen: set[str] = set()
_DEDUPED_PROBES: list[str] = []
for _p in DANGER_PROBES:
    if _p not in _seen:
        _DEDUPED_PROBES.append(_p)
        _seen.add(_p)
DANGER_PROBES = _DEDUPED_PROBES

# ---------------------------------------------------------------------------
# Known polysemous terms
# ---------------------------------------------------------------------------
# These terms are semantically wrong even at word boundaries in exclude_any.
# "appeal" means both "to apply to a higher court" and "to attract/please".
# A tenant who says "this does not appeal to me" should not have the route
# suppressed.
POLYSEMOUS_EXCLUDE_ANY: set[str] = {
    "appeal", "appealing", "appeals", "appealed",
}

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

MATCHABLE_FIELDS: list[str] = [
    "include_any_precise",
    "include_any_broad",
    "require_context_any",
    "include_any",
    "include_all",
    "exclude_any",
]


def _is_route_word_char(c: str) -> bool:
    return ("a" <= c <= "z") or ("0" <= c <= "9") or c == "_"


def _matches_probe_at_non_boundary(term: str, probe: str) -> bool:
    """True if term appears inside probe at a position that fails boundary check."""
    tlen = len(term)
    plen = len(probe)
    start = 0
    while True:
        idx = probe.find(term, start)
        if idx == -1:
            return False
        end = idx + tlen
        left_ok = idx == 0 or not _is_route_word_char(probe[idx - 1])
        right_ok = end == plen or not _is_route_word_char(probe[end])
        if not (left_ok and right_ok):
            return True  # non-boundary match found
        start = idx + 1


def _danger_probes_hit(term: str) -> list[str]:
    return [p for p in DANGER_PROBES if _matches_probe_at_non_boundary(term, p)]


def _is_short_bare_alpha(term: str) -> bool:
    """Single word, all lowercase letters, length <= 5."""
    return " " not in term and "-" not in term and term.isalpha() and len(term) <= 5


# ---------------------------------------------------------------------------
# Finding dataclass
# ---------------------------------------------------------------------------

LEVEL_HARD_FAIL = "HARD_FAIL"
LEVEL_WARN = "WARN"


@dataclass
class Finding:
    level: str          # HARD_FAIL | WARN
    rule: str           # short rule tag
    intent: str
    field: str
    term: str
    detail: str         # human-readable explanation

    def __str__(self) -> str:
        tag = f"[{self.level}]" if self.level == LEVEL_HARD_FAIL else f"[{self.level:>4}]"
        return (
            f"{tag} {self.rule:30} {self.intent}/{self.field}\n"
            f"       term: {self.term!r}\n"
            f"       {self.detail}"
        )


# ---------------------------------------------------------------------------
# Whitelist
# ---------------------------------------------------------------------------

@dataclass
class WhitelistEntry:
    route: str      # intent or "*" for all routes
    field: str      # field name or "*" for all fields
    term: str
    reason: str


def _load_whitelist(path: pathlib.Path) -> list[WhitelistEntry]:
    if not path.exists():
        return []
    with path.open() as f:
        data = json.load(f)
    return [WhitelistEntry(**e) for e in data]


def _is_whitelisted(
    entry: WhitelistEntry,
    intent: str,
    field: str,
    term: str,
) -> bool:
    route_match = entry.route in ("*", intent)
    field_match = entry.field in ("*", field)
    term_match = entry.term == term
    return route_match and field_match and term_match


def _whitelisted(
    whitelist: list[WhitelistEntry],
    intent: str,
    field: str,
    term: str,
) -> WhitelistEntry | None:
    for e in whitelist:
        if _is_whitelisted(e, intent, field, term):
            return e
    return None


# ---------------------------------------------------------------------------
# Main lint pass
# ---------------------------------------------------------------------------

def lint_routes(routes: list, whitelist: list[WhitelistEntry]) -> list[Finding]:
    findings: list[Finding] = []

    for route in routes:
        intent = route.intent

        for field_name in MATCHABLE_FIELDS:
            terms: tuple[str, ...] = getattr(route, field_name, ())
            seen_in_field: set[str] = set()

            for term in terms:
                wl = _whitelisted(whitelist, intent, field_name, term)

                # --- empty term ---
                if not term:
                    findings.append(Finding(
                        level=LEVEL_HARD_FAIL,
                        rule="empty_term",
                        intent=intent,
                        field=field_name,
                        term=term,
                        detail="Empty string in route field.",
                    ))
                    continue

                # --- duplicate term in same field ---
                if term in seen_in_field:
                    findings.append(Finding(
                        level=LEVEL_HARD_FAIL,
                        rule="duplicate_term",
                        intent=intent,
                        field=field_name,
                        term=term,
                        detail=f"Term appears more than once in {field_name}.",
                    ))
                seen_in_field.add(term)

                if wl:
                    continue  # all other checks suppressed by whitelist entry

                # --- polysemous term in exclude_any ---
                if field_name == "exclude_any" and term in POLYSEMOUS_EXCLUDE_ANY:
                    findings.append(Finding(
                        level=LEVEL_HARD_FAIL,
                        rule="polysemous_exclude",
                        intent=intent,
                        field=field_name,
                        term=term,
                        detail=(
                            f"'{term}' is polysemous: it has a non-legal everyday meaning "
                            f"('this does not {term} to me'). Even at word boundaries it "
                            f"silently suppresses routes for unrelated queries. "
                            f"Replace with specific legal phrases "
                            f"(e.g., 'appeal the decision', 'notice of appeal')."
                        ),
                    ))

                # --- danger probe match in exclude_any (hard fail) ---
                if field_name == "exclude_any":
                    hits = _danger_probes_hit(term)
                    if hits:
                        findings.append(Finding(
                            level=LEVEL_HARD_FAIL,
                            rule="danger_probe_exclude",
                            intent=intent,
                            field=field_name,
                            term=term,
                            detail=(
                                f"Term appears as a non-boundary substring in: "
                                f"{', '.join(repr(h) for h in hits[:5])}. "
                                f"In exclude_any this would silently suppress the route "
                                f"for unrelated queries if the boundary check is bypassed. "
                                f"Use a specific multi-word phrase instead."
                            ),
                        ))

                # --- short bare alpha in exclude_any (hard fail without whitelist) ---
                if field_name == "exclude_any" and _is_short_bare_alpha(term):
                    findings.append(Finding(
                        level=LEVEL_HARD_FAIL,
                        rule="short_alpha_exclude",
                        intent=intent,
                        field=field_name,
                        term=term,
                        detail=(
                            f"Short single-word alphabetic term (len={len(term)}) in "
                            f"exclude_any. These terms are high-risk: a single letter "
                            f"typo or future bypass of boundary checking silently "
                            f"suppresses the route. Use a more specific phrase, or "
                            f"add a whitelist entry with a documented reason."
                        ),
                    ))

                # --- danger probe match in non-exclude fields (warning) ---
                if field_name != "exclude_any":
                    hits = _danger_probes_hit(term)
                    if hits:
                        findings.append(Finding(
                            level=LEVEL_WARN,
                            rule="danger_probe_include",
                            intent=intent,
                            field=field_name,
                            term=term,
                            detail=(
                                f"Term appears as a non-boundary substring in: "
                                f"{', '.join(repr(h) for h in hits[:3])}. "
                                f"Safe under boundary-aware matcher but risky if that "
                                f"matcher is bypassed. Add whitelist entry if intentional."
                            ),
                        ))

                # --- short bare alpha in include_any / broad / require_context (warning) ---
                if field_name in ("include_any", "include_any_broad", "require_context_any",
                                  "include_any_precise", "include_all") and _is_short_bare_alpha(term):
                    findings.append(Finding(
                        level=LEVEL_WARN,
                        rule="short_alpha_include",
                        intent=intent,
                        field=field_name,
                        term=term,
                        detail=(
                            f"Short single-word term (len={len(term)}) in {field_name}. "
                            f"Safe under boundary-aware matcher but document intent. "
                            f"Add whitelist entry to suppress this warning."
                        ),
                    ))

    return findings


# ---------------------------------------------------------------------------
# Report formatting
# ---------------------------------------------------------------------------

def _print_summary(routes: list, all_terms: list[tuple], findings: list[Finding]) -> None:
    hard_fails = [f for f in findings if f.level == LEVEL_HARD_FAIL]
    warns = [f for f in findings if f.level == LEVEL_WARN]
    print(
        f"Routes: {len(routes)}  "
        f"Terms: {len(all_terms)}  "
        f"Hard-fails: {len(hard_fails)}  "
        f"Warnings: {len(warns)}"
    )
    print()

    if hard_fails:
        print("=== HARD FAILS ===")
        for f in hard_fails:
            print(str(f))
            print()

    if warns:
        print("=== WARNINGS ===")
        for f in warns:
            print(str(f))
            print()

    if not findings:
        print("OK - no findings.")


def _collect_terms(routes: list) -> list[tuple]:
    terms = []
    for r in routes:
        for f in MATCHABLE_FIELDS:
            for t in getattr(r, f, ()):
                terms.append((r.intent, f, t))
    return terms


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=["report", "ci"], default="report",
                        help="report: print only, no exit code; ci: exit 1 on hard-fail")
    parser.add_argument("--jurisdiction", default="nz_tenancy",
                        help="Jurisdiction to lint (default: nz_tenancy)")
    parser.add_argument("--whitelist",
                        default=str(pathlib.Path(__file__).parent / "lint_whitelist.json"),
                        help="Path to whitelist JSON file")
    parser.add_argument("--astraea-py", default=None,
                        help="Path to the Python astraea checkout (overrides ASTRAEA_PY_PATH)")
    args = parser.parse_args()

    astraea_py = _find_astraea_py(args.astraea_py)
    _bootstrap(astraea_py)

    # Import routes after path setup
    if args.jurisdiction == "nz_tenancy":
        from jurisdictions.nz_tenancy.routes import ROUTES as routes
    else:
        print(f"ERROR: Unknown jurisdiction {args.jurisdiction!r}", file=sys.stderr)
        return 2

    whitelist_path = pathlib.Path(args.whitelist)
    whitelist = _load_whitelist(whitelist_path)

    all_terms = _collect_terms(routes)
    findings = lint_routes(routes, whitelist)
    _print_summary(routes, all_terms, findings)

    hard_fail_count = sum(1 for f in findings if f.level == LEVEL_HARD_FAIL)

    if args.mode == "ci" and hard_fail_count > 0:
        print(f"CI FAIL: {hard_fail_count} hard-fail finding(s). Fix routes or add whitelist entries.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
