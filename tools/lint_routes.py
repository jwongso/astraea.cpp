#!/usr/bin/env python3
"""
tools/lint_routes.py - CI linter for NZ tenancy route + LP-section terms.

Scans jurisdictions/<jurisdiction>/routes.cpp directly (no Python reference
repo needed) for term-safety violations:

  - empty / duplicate terms in any matchable field
  - polysemous bare terms in exclude_any  (e.g. "appeal")
  - short bare alphabetic terms in exclude_any  (length <= 5, no whitelist)
  - terms that would non-boundary-match common English words
    (regression signal if the AC engine ever loses its boundary check)
  - same set of checks for LOW_PRIORITY_SECTIONS terms (PR foundation-2:
    those terms gate section visibility just like exclude_any)

Usage:
    python tools/lint_routes.py [--mode report|ci]
                                [--routes jurisdictions/nz_tenancy/routes.cpp]
                                [--whitelist tools/lint_whitelist.json]

Modes:
    report  print findings; exit 0 regardless of findings (default)
    ci      print findings; exit 1 if any HARD_FAIL findings exist

This linter is the C++-source counterpart to the previous Python-coupled
linter. It removes the dependency on the (now-dormant) Python astraea repo
and adds coverage for LOW_PRIORITY_SECTIONS, which the previous linter
never scanned.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass

# ---------------------------------------------------------------------------
# Danger probe corpus
# ---------------------------------------------------------------------------
# Common English or NZ-tenancy words that contain short route terms as
# NON-BOUNDARY substrings. Even with the boundary-aware AC matcher in place,
# adding a term that hits one of these is a risk signal: if the matcher is
# ever bypassed or the term is reused in a field that filters non-boundary,
# silent bugs return.

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
    # view
    "reviewing", "reviewed",
    # paint
    "repainting", "repainted",
    # alter
    "alternative", "alternatives", "alteration", "alterations",
    # bond
    "bonded", "bonding",
    # fix
    "fixture", "fixtures", "prefix", "affix",
    # lock
    "clockwork", "flocking", "unlock", "gridlock",
    # notice
    "unnoticed",
    # land
    "landlord", "landlords",
    # rent
    "current", "torrent", "different", "parent",
    # lease
    "release", "unleash",
    # end
    "render", "blender", "lender", "extended", "amendment",
    # old
    "household", "withheld",
    # own
    "known", "brown", "town", "downtown",
    # run
    "running", "overrun", "truncate",
    # pet
    "petrol", "competitive", "carpet",
    # cat
    "caterpillar", "locate", "education", "indicate",
    # dog
    "hotdog",
    # oven
    "coven",
    # damp
    "dampening",
    # flush
    "blushing",
    # harm / hit / pharm (LP-relevant)
    "pharmacy", "charm", "harmless", "harmful", "harmony",
]
DANGER_PROBES = list(dict.fromkeys(DANGER_PROBES))  # dedupe, preserve order

POLYSEMOUS_EXCLUDE_ANY: set[str] = {
    "appeal", "appealing", "appeals", "appealed",
}

# ---------------------------------------------------------------------------
# Boundary predicate — mirror of is_route_word_char() in
# include/astraea/term_match.hpp.
# ---------------------------------------------------------------------------

def _is_route_word_char(c: str) -> bool:
    return ("a" <= c <= "z") or ("0" <= c <= "9") or c == "_"


def _non_boundary_match(term: str, text: str) -> bool:
    """True if `term` appears inside `text` at any position whose left or
    right neighbour is a route-word char (i.e. would have been a false-positive
    under raw substring matching)."""
    tlen = len(term)
    if tlen == 0 or tlen > len(text):
        return False
    start = 0
    while True:
        idx = text.find(term, start)
        if idx == -1:
            return False
        end = idx + tlen
        left_ok = idx == 0 or not _is_route_word_char(text[idx - 1])
        right_ok = end == len(text) or not _is_route_word_char(text[end])
        if not (left_ok and right_ok):
            return True
        start = idx + 1


def _danger_probes_hit(term: str) -> list[str]:
    return [p for p in DANGER_PROBES if _non_boundary_match(term, p)]


def _is_short_bare_alpha(term: str) -> bool:
    return " " not in term and "-" not in term and term.isalpha() and len(term) <= 5


# ---------------------------------------------------------------------------
# C++ source parsing
# ---------------------------------------------------------------------------
# The routes table is a vector of designated-initialised StatuteRoute structs.
# Each struct opens with `.intent = "..."` and we extract field-by-field via
# balanced-brace scanning. This is purposely tolerant — comments and trailing
# commas are stripped, but the parser does NOT try to be a full C++ tokeniser.
# The route file already passes a Catch2 build; this script only needs to lift
# string literals out of the field lists.

MATCHABLE_FIELDS: tuple[str, ...] = (
    "include_any_precise",
    "include_any_broad",
    "require_context_any",
    "include_any",
    "include_all",
    "exclude_any",
)

_INTENT_RE  = re.compile(r'\.intent\s*=\s*"([^"]+)"')
_FIELD_RE   = {f: re.compile(rf'\.{f}\s*=\s*\{{') for f in MATCHABLE_FIELDS}
_STRING_RE  = re.compile(r'"([^"]*)"')
_COMMENT_RE = re.compile(r'//[^\n]*')


def _strip_comments(text: str) -> str:
    return _COMMENT_RE.sub("", text)


def _find_balanced(text: str, open_idx: int) -> int:
    """Return index just past the matching close-brace for the open-brace at
    `open_idx`. Raises ValueError if unbalanced."""
    assert text[open_idx] == "{"
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == '"':  # skip over string literal
            j = text.find('"', i + 1)
            if j == -1:
                raise ValueError("unterminated string literal")
            i = j
        i += 1
    raise ValueError("unbalanced braces")


@dataclass
class Route:
    intent: str
    fields: dict[str, tuple[str, ...]]  # field_name -> term tuple


def _parse_routes(source_path: pathlib.Path) -> list[Route]:
    text = _strip_comments(source_path.read_text())
    routes: list[Route] = []

    # Split the source into per-route chunks at .intent boundaries. The slice
    # ends at the next .intent or at end-of-file.
    intents = [(m.start(), m.group(1)) for m in _INTENT_RE.finditer(text)]
    # Only consider intents inside ROUTES (i.e. before LOW_PRIORITY_SECTIONS).
    lp_marker = text.find("LOW_PRIORITY_SECTIONS")
    if lp_marker != -1:
        intents = [(p, n) for p, n in intents if p < lp_marker]

    for i, (pos, intent) in enumerate(intents):
        end = intents[i + 1][0] if i + 1 < len(intents) else len(text)
        chunk = text[pos:end]

        fields: dict[str, tuple[str, ...]] = {f: () for f in MATCHABLE_FIELDS}
        for field, pat in _FIELD_RE.items():
            m = pat.search(chunk)
            if not m:
                continue
            brace = chunk.index("{", m.end() - 1)
            close = _find_balanced(chunk, brace)
            body = chunk[brace + 1: close - 1]
            fields[field] = tuple(_STRING_RE.findall(body))
        routes.append(Route(intent=intent, fields=fields))

    return routes


def _parse_low_priority_sections(
    source_path: pathlib.Path,
) -> list[tuple[str, tuple[str, ...]]]:
    """Return [(section_id, (term, ...)), ...] for LOW_PRIORITY_SECTIONS."""
    text = _strip_comments(source_path.read_text())
    marker = text.find("LOW_PRIORITY_SECTIONS")
    if marker == -1:
        return []
    open_brace = text.index("{", marker)
    close = _find_balanced(text, open_brace)
    body = text[open_brace + 1: close - 1]

    # Each entry is `{ "SECTION/ID", { "term1", "term2", ... }, },` — find
    # outer braces, then within each outer brace pull the section id (first
    # string) and the inner brace's strings.
    results: list[tuple[str, tuple[str, ...]]] = []
    i = 0
    n = len(body)
    while i < n:
        if body[i] != "{":
            i += 1
            continue
        outer_close = _find_balanced(body, i)
        entry = body[i + 1: outer_close - 1]
        strings = _STRING_RE.findall(entry)
        if strings:
            section_id = strings[0]
            # Terms live inside the inner brace; pull all strings after the id.
            results.append((section_id, tuple(strings[1:])))
        i = outer_close
    return results


# ---------------------------------------------------------------------------
# Findings + whitelist
# ---------------------------------------------------------------------------

LEVEL_HARD_FAIL = "HARD_FAIL"
LEVEL_WARN = "WARN"


@dataclass
class Finding:
    level: str
    rule: str
    intent: str
    field: str
    term: str
    detail: str

    def __str__(self) -> str:
        tag = f"[{self.level}]" if self.level == LEVEL_HARD_FAIL else f"[{self.level:>4}]"
        return (
            f"{tag} {self.rule:25} {self.intent}/{self.field}\n"
            f"       term: {self.term!r}\n"
            f"       {self.detail}"
        )


@dataclass
class WhitelistEntry:
    route: str
    field: str
    term: str
    reason: str


def _load_whitelist(path: pathlib.Path) -> list[WhitelistEntry]:
    if not path.exists():
        return []
    return [WhitelistEntry(**e) for e in json.loads(path.read_text())]


def _whitelisted(
    whitelist: list[WhitelistEntry], intent: str, field: str, term: str,
) -> WhitelistEntry | None:
    for e in whitelist:
        if (e.route in ("*", intent)
                and e.field in ("*", field)
                and e.term == term):
            return e
    return None


# ---------------------------------------------------------------------------
# Lint passes
# ---------------------------------------------------------------------------

def _lint_term(
    intent: str, field: str, term: str, seen_in_field: set[str],
    whitelist: list[WhitelistEntry], findings: list[Finding],
    *,
    is_lp_section: bool = False,
) -> None:
    """Apply the term-safety rule set to a single (intent, field, term)."""
    if not term:
        findings.append(Finding(
            LEVEL_HARD_FAIL, "empty_term", intent, field, term,
            "Empty string in route/LP-section field."))
        return

    if term in seen_in_field:
        findings.append(Finding(
            LEVEL_HARD_FAIL, "duplicate_term", intent, field, term,
            f"Term appears more than once in {field}."))
    seen_in_field.add(term)

    if _whitelisted(whitelist, intent, field, term):
        return

    # exclude_any and LP terms have the same severity profile: a false
    # match silently suppresses or surfaces a section. Treat both alike.
    is_suppress_field = (field == "exclude_any") or is_lp_section

    if is_suppress_field and term in POLYSEMOUS_EXCLUDE_ANY:
        findings.append(Finding(
            LEVEL_HARD_FAIL, "polysemous_suppress", intent, field, term,
            f"'{term}' is polysemous: has a non-legal everyday meaning "
            f"('this does not {term} to me'). Replace with a specific "
            f"phrase ('appeal the decision', 'notice of appeal')."))

    probes = _danger_probes_hit(term)
    if is_suppress_field and probes:
        findings.append(Finding(
            LEVEL_HARD_FAIL, "danger_probe_suppress", intent, field, term,
            f"Term appears as a non-boundary substring in "
            f"{', '.join(repr(p) for p in probes[:5])}. "
            f"In {'exclude_any' if field == 'exclude_any' else 'LP gate'} "
            f"this would silently mis-route if boundary checking is bypassed. "
            f"Use a multi-word phrase, or whitelist with a documented reason."))

    if is_suppress_field and _is_short_bare_alpha(term):
        findings.append(Finding(
            LEVEL_HARD_FAIL, "short_alpha_suppress", intent, field, term,
            f"Short single-word alphabetic term (len={len(term)}) in "
            f"{'exclude_any' if field == 'exclude_any' else 'LP gate'}. "
            f"High-risk: a single typo or boundary-check bypass silently "
            f"mis-routes. Use a specific phrase or whitelist with reason."))

    if not is_suppress_field and probes:
        findings.append(Finding(
            LEVEL_WARN, "danger_probe_include", intent, field, term,
            f"Term appears as a non-boundary substring in "
            f"{', '.join(repr(p) for p in probes[:3])}. "
            f"Safe under boundary-aware matcher but risky if bypassed."))

    if not is_suppress_field and _is_short_bare_alpha(term):
        findings.append(Finding(
            LEVEL_WARN, "short_alpha_include", intent, field, term,
            f"Short single-word term (len={len(term)}) in {field}. "
            f"Safe under boundary-aware matcher but document intent."))


def lint_routes(
    routes: list[Route],
    lp_sections: list[tuple[str, tuple[str, ...]]],
    whitelist: list[WhitelistEntry],
) -> list[Finding]:
    findings: list[Finding] = []

    for route in routes:
        for field in MATCHABLE_FIELDS:
            seen: set[str] = set()
            for term in route.fields.get(field, ()):
                _lint_term(route.intent, field, term, seen, whitelist, findings)

    for section_id, terms in lp_sections:
        seen: set[str] = set()
        for term in terms:
            _lint_term(section_id, "low_priority_sections", term, seen,
                       whitelist, findings, is_lp_section=True)

    return findings


# ---------------------------------------------------------------------------
# Reporting + entry point
# ---------------------------------------------------------------------------

def _print_summary(
    routes: list[Route],
    lp_sections: list[tuple[str, tuple[str, ...]]],
    findings: list[Finding],
) -> None:
    n_terms = sum(len(t) for r in routes for t in r.fields.values())
    n_lp = sum(len(t) for _, t in lp_sections)
    hard = [f for f in findings if f.level == LEVEL_HARD_FAIL]
    warn = [f for f in findings if f.level == LEVEL_WARN]
    print(
        f"Routes: {len(routes)}  Route-terms: {n_terms}  "
        f"LP-sections: {len(lp_sections)}  LP-terms: {n_lp}  "
        f"Hard-fails: {len(hard)}  Warnings: {len(warn)}"
    )
    print()
    for group, label in ((hard, "=== HARD FAILS ==="), (warn, "=== WARNINGS ===")):
        if group:
            print(label)
            for f in group:
                print(str(f))
                print()
    if not findings:
        print("OK - no findings.")


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    default_routes = repo_root / "jurisdictions" / "nz_tenancy" / "routes.cpp"
    default_whitelist = repo_root / "tools" / "lint_whitelist.json"

    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--mode", choices=["report", "ci"], default="report",
                   help="report: print only; ci: exit 1 on hard-fail")
    p.add_argument("--routes", default=str(default_routes),
                   help="Path to the routes.cpp file to scan")
    p.add_argument("--whitelist", default=str(default_whitelist),
                   help="Path to whitelist JSON file")
    args = p.parse_args()

    routes_path = pathlib.Path(args.routes)
    if not routes_path.exists():
        print(f"ERROR: routes file not found: {routes_path}", file=sys.stderr)
        return 2

    routes = _parse_routes(routes_path)
    lp_sections = _parse_low_priority_sections(routes_path)
    whitelist = _load_whitelist(pathlib.Path(args.whitelist))

    findings = lint_routes(routes, lp_sections, whitelist)
    _print_summary(routes, lp_sections, findings)

    hard = sum(1 for f in findings if f.level == LEVEL_HARD_FAIL)
    if args.mode == "ci" and hard > 0:
        print(f"CI FAIL: {hard} hard-fail finding(s). Fix routes or whitelist.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
