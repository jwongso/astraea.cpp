r"""Fixture loaders for the eval delta viewer.

Three on-disk shapes get normalised into one internal fixture schema:

  shape A — astraea.cpp repo golden  (tests/integration/golden/g*.json)
      Hand-curated, richest: required_sections, forbidden_claims, key_points,
      citations.legislation[*].{section,title,url,key_rule}. Currently 2 items.

  shape B — Python test split  (~/proj/priv/astraea/.training/splits/test.jsonl)
      50 items. Each row has post_id, question, bruce_answer (human reference).
      No structured asks: we derive required_sections via the section-ref
      regex over bruce_answer, and treat each substantive sentence in
      bruce_answer as a key_point.

  shape C — oracle judge output  (~/proj/priv/astraea/.training/oracle_results_A.jsonl)
      Overlay on top of shape B for the subset that's been judged (15 items
      today). Provides issue_labels and the canonical
      must_include_hits/must_include_misses (combined as key_points).

Normalised fixture (what the viewer/analyzer consume):

    {
        "id":       str,            # post_id or g001
        "source":   "repo_golden" | "python_eval" | "python_eval_oracle",
        "topic":    str,
        "question": str,
        "golden": {
            "answer":             str,             # reference answer
            "required_sections":  list[str],       # ["s48", ...]
            "forbidden_claims":   list[str],
            "key_points":         list[str],
            "citations":          dict,            # {"legislation": [...], "cases": [...]}
        },
        "path":     str,
        "key_point_origin": str,    # provenance string for the frontend badge
    }

Section extraction: ``\b s(\d+) (\w?) (?:\([^)]+\))* \b`` — matches s48, s49A,
s51A(2), s40(3), "section 24(1)(e)" (the "section NN" form is normalised
to "sNN" for parity with repo golden).
"""
from __future__ import annotations

import json
import os
import pathlib
import re
from typing import Any


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO     = pathlib.Path(__file__).resolve().parent.parent.parent
GOLDEN_DIR_DEFAULT = REPO / "tests" / "integration" / "golden"
PY_ROOT_DEFAULT    = pathlib.Path.home() / "proj" / "priv" / "astraea"


def py_root() -> pathlib.Path:
    return pathlib.Path(os.environ.get("ASTRAEA_PY_ROOT", PY_ROOT_DEFAULT))


# ---------------------------------------------------------------------------
# Section / sentence extraction (deterministic; no LLM)
# ---------------------------------------------------------------------------

_SECTION_RE = re.compile(
    r"""
    \b
    (?:                            # either "sNN" or "section NN"
        s
      | [Ss]ection \s+
    )
    (\d+)([A-Z]?)                  # number + optional letter (s49A)
    (                              # optional (sub)(sub) suffixes
        (?: \( [^)]+ \) )*
    )
    \b
    """,
    re.VERBOSE,
)


def extract_sections(text: str, *, max_n: int = 12) -> list[str]:
    """Return unique section references in order of first appearance.

    Normalises "section 24(1)(e)" -> "s24(1)(e)" so the output matches the
    convention used by tests/integration/golden/*.json's required_sections.
    """
    if not text:
        return []
    seen: dict[str, None] = {}
    for m in _SECTION_RE.finditer(text):
        num, letter, suffix = m.group(1), m.group(2), m.group(3) or ""
        ref = f"s{num}{letter}{suffix}"
        if ref not in seen:
            seen[ref] = None
        if len(seen) >= max_n:
            break
    return list(seen.keys())


# Sentence splitter: handles ". ", "! ", "? ", and bullet/list markers.
# Conservative; we lose nothing critical because every sentence is then
# filtered for substance (non-stopword token count).
_SENT_SPLIT = re.compile(r"(?<=[.!?])\s+(?=[A-Z(])|\n\s*[-*•]\s+|\n{2,}")


_STOP = {
    "the", "and", "or", "but", "is", "are", "was", "were", "be", "been",
    "have", "has", "had", "do", "does", "did", "will", "would", "could",
    "should", "may", "might", "must", "shall", "not", "no", "so", "if",
    "in", "on", "at", "to", "for", "of", "with", "this", "that", "it",
    "its", "by", "from", "as", "than", "also", "any", "all", "more",
    "such", "under", "into", "about", "when", "only", "then", "here",
    "their", "they", "what", "which", "who", "your", "you", "our", "we",
}


def _substantive_token_count(s: str) -> int:
    return sum(1 for w in re.findall(r"[a-z0-9]+", s.lower())
               if w not in _STOP and len(w) > 2)


def extract_key_points(answer: str, *, min_tokens: int = 5,
                       max_points: int = 14) -> list[str]:
    """Split a free-text reference answer into atomic substantive claims.

    A "key_point" is a sentence (or bullet) with at least min_tokens
    substantive (non-stopword, ≥3-char) tokens. Salutations and
    sign-offs are dropped by the token-count filter naturally.
    """
    if not answer:
        return []
    parts = _SENT_SPLIT.split(answer)
    out: list[str] = []
    for raw in parts:
        s = " ".join(raw.replace("\n", " ").split()).strip(" -*•")
        if not s:
            continue
        if _substantive_token_count(s) < min_tokens:
            continue
        # Drop obvious salutations / closings.
        head_low = s[:40].lower()
        if any(head_low.startswith(p) for p in
               ("hi ", "hello ", "hey ", "kia ora", "dear ", "thanks ",
                "thank you", "regards", "cheers")):
            continue
        out.append(s)
        if len(out) >= max_points:
            break
    return out


# ---------------------------------------------------------------------------
# Loaders — return the normalised fixture shape
# ---------------------------------------------------------------------------

def _safe_relpath(p: pathlib.Path) -> str:
    """Return p relative to REPO when possible, else absolute string."""
    try:
        return str(p.resolve().relative_to(REPO))
    except (ValueError, OSError):
        return str(p)


def _repo_golden_one(p: pathlib.Path) -> dict[str, Any]:
    g = json.loads(p.read_text())
    return {
        "id":       g["id"],
        "source":   "repo_golden",
        "topic":    g.get("topic", ""),
        "question": g["question"],
        "golden": {
            "answer":            g.get("answer", "") or "",
            "required_sections": list(g.get("required_sections", []) or []),
            "forbidden_claims":  list(g.get("forbidden_claims", []) or []),
            "key_points":        list(g.get("key_points", []) or []),
            "citations":         dict(g.get("citations", {}) or {}),
        },
        "path":              _safe_relpath(p),
        "key_point_origin":  "hand-curated",
    }


def load_repo_golden(golden_dir: pathlib.Path) -> list[dict[str, Any]]:
    files = sorted(golden_dir.glob("*.json"))
    return [_repo_golden_one(p) for p in files]


def _load_jsonl(path: pathlib.Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    out = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return out


def _python_one(test_row: dict[str, Any],
                oracle_row: dict[str, Any] | None) -> dict[str, Any]:
    answer = test_row.get("bruce_answer", "") or ""
    required = extract_sections(answer)
    forbidden: list[str] = []   # not encoded on the python side
    citations: dict[str, Any] = {}

    if oracle_row:
        orig = (oracle_row.get("orig_scores") or {})
        hits   = list(orig.get("must_include_hits",   []) or [])
        misses = list(orig.get("must_include_misses", []) or [])
        key_points = hits + misses
        origin = (f"oracle judge ({len(hits)} hits + {len(misses)} misses)"
                  if key_points else "(oracle row had no must_include)")
        labels = oracle_row.get("issue_labels", []) or []
        topic = labels[0] if labels else (test_row.get("group", "") or "")
        source = "python_eval_oracle"
        # If oracle declared an exhaustive must_include list and we extracted
        # no sections from bruce_answer, also scan the must_include items
        # themselves — judges often mention "s48" or "section 48" in the
        # rule statements.
        if not required and key_points:
            combo = " ".join(key_points)
            required = extract_sections(combo)
    else:
        key_points = extract_key_points(answer)
        origin = f"derived from bruce_answer ({len(key_points)} sentences)"
        topic = test_row.get("group", "") or ""
        source = "python_eval"

    return {
        "id":       str(test_row.get("post_id", "")),
        "source":   source,
        "topic":    topic,
        "question": test_row.get("question", "") or "",
        "golden": {
            "answer":            answer,
            "required_sections": required,
            "forbidden_claims":  forbidden,
            "key_points":        key_points,
            "citations":         citations,
        },
        "path":             "training/splits/test.jsonl",
        "key_point_origin": origin,
    }


def load_python_eval(*, py_root: pathlib.Path | None = None
                     ) -> list[dict[str, Any]]:
    root = py_root or globals()["py_root"]()
    test = _load_jsonl(root / ".training" / "splits" / "test.jsonl")
    oracle = _load_jsonl(root / ".training" / "oracle_results_A.jsonl")
    oracle_by_id = {str(r.get("post_id", "")): r for r in oracle}
    out = []
    for row in test:
        pid = str(row.get("post_id", ""))
        out.append(_python_one(row, oracle_by_id.get(pid)))
    return out


def load_all(*, golden_dir: pathlib.Path,
             py_root_path: pathlib.Path | None = None
             ) -> list[dict[str, Any]]:
    """Union: repo_golden first, then python_eval, dedup by id."""
    seen: set[str] = set()
    out: list[dict[str, Any]] = []
    for fx in load_repo_golden(golden_dir):
        if fx["id"] in seen:
            continue
        seen.add(fx["id"])
        out.append(fx)
    for fx in load_python_eval(py_root=py_root_path):
        if fx["id"] in seen:
            continue
        seen.add(fx["id"])
        out.append(fx)
    return out
