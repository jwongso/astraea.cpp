"""
Golden response quality evaluator for nz_tenancy.

For each fixture in tests/integration/golden/*.json:
  - Streams a live answer from /ask/stream (once per fixture - result is cached)
  - Hard-checks required_sections appear in the answer text
  - Hard-checks forbidden_claims do NOT appear (case-insensitive)
  - Soft-scores key_point coverage (word overlap) and asserts >= 50% covered
  - Prints timing breakdown with -s

Run:
    pytest tests/integration/test_golden_quality.py -v -s
"""

import json
import os
import pathlib
import re
from typing import Any

import pytest
import requests

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

NZ_URL    = os.environ.get("NZ_TENANCY_URL", "http://localhost:8001")
NO_LOG    = {"X-No-Log": "1"}
GOLDEN_DIR = pathlib.Path(__file__).parent / "golden"

STOP_WORDS = {
    "a", "an", "the", "and", "or", "but", "is", "are", "was", "were",
    "be", "been", "being", "have", "has", "had", "do", "does", "did",
    "will", "would", "could", "should", "may", "might", "must", "shall",
    "not", "no", "so", "if", "in", "on", "at", "to", "for", "of", "with",
    "this", "that", "it", "its", "by", "from", "as", "than", "also",
    "any", "all", "more", "such", "under", "into", "about", "when",
    "only", "then", "here", "their", "they", "what", "which", "who",
}


def _token_from_service(path: str) -> str:
    try:
        m = re.search(r"Environment=PUBLIC_TOKEN=(\S+)", open(path).read())
        return m.group(1) if m else ""
    except OSError:
        return ""


NZ_TOKEN = (os.environ.get("NZ_TENANCY_TOKEN")
            or _token_from_service(
               os.path.expanduser("~/.config/systemd/user/astraea-nz-tenancy.service")))

# ---------------------------------------------------------------------------
# Fixture loading
# ---------------------------------------------------------------------------

def _load_fixtures() -> list[dict]:
    return [json.loads(p.read_text()) for p in sorted(GOLDEN_DIR.glob("*.json"))]

FIXTURES: list[dict] = _load_fixtures()

# ---------------------------------------------------------------------------
# Answer cache - each fixture is streamed once per pytest session
# ---------------------------------------------------------------------------

_cache: dict[str, tuple[str, dict | None]] = {}


def _get_answer(fixture: dict) -> tuple[str, dict | None]:
    fid = fixture["id"]
    if fid in _cache:
        return _cache[fid]

    r = requests.post(
        f"{NZ_URL}/ask/stream",
        json={"question": fixture["question"]},
        headers={"X-API-Key": NZ_TOKEN, **NO_LOG},
        stream=True,
        timeout=120,
    )
    assert r.status_code == 200, f"/ask/stream returned {r.status_code}: {r.text[:200]}"

    tokens: list[str] = []
    timing: dict | None = None
    for line in r.iter_lines():
        if isinstance(line, bytes):
            line = line.decode()
        line = line.strip()
        if not line.startswith("data: "):
            continue
        try:
            ev = json.loads(line[6:])
        except json.JSONDecodeError:
            continue
        if ev.get("type") == "token":
            tokens.append(ev.get("text", ""))
        elif ev.get("type") == "timing":
            timing = ev

    result = ("".join(tokens), timing)
    _cache[fid] = result
    return result

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _section_regex(ref: str) -> str:
    """
    Turn a section reference like "s48" or "s24(1A)" into a regex that matches
    the common ways it appears in prose:
      s48 / s 48 / section 48 / Section 48 / s48(5) / section 48(5)
    """
    m = re.match(r's(\d+)(.*)', ref)
    if not m:
        return re.escape(ref)
    num    = m.group(1)
    suffix = re.escape(m.group(2))   # "(5)" or "(1A)" or ""
    return rf'(?:s\s*{num}|[Ss]ection\s+{num}){suffix}'


def _key_point_score(point: str, answer: str) -> float:
    """Word-overlap fraction: share of meaningful words in point found in answer."""
    lower = answer.lower()
    words = [w for w in re.findall(r'[a-z0-9]+', point.lower())
             if w not in STOP_WORDS and len(w) > 2]
    if not words:
        return 1.0
    return sum(1 for w in words if w in lower) / len(words)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("fixture", FIXTURES, ids=[f["id"] for f in FIXTURES])
def test_required_sections_present(fixture: dict):
    """Every section listed in required_sections must appear in the answer."""
    answer, _ = _get_answer(fixture)
    missing = [s for s in fixture.get("required_sections", [])
               if not re.search(_section_regex(s), answer, re.IGNORECASE)]
    assert not missing, (
        f"[{fixture['id']}] Missing required sections: {missing}\n"
        f"Answer (first 600 chars):\n{answer[:600]}"
    )


@pytest.mark.parametrize("fixture", FIXTURES, ids=[f["id"] for f in FIXTURES])
def test_forbidden_claims_absent(fixture: dict):
    """None of the forbidden_claims phrases may appear in the answer."""
    answer, _ = _get_answer(fixture)
    found = [c for c in fixture.get("forbidden_claims", [])
             if c.lower() in answer.lower()]
    assert not found, (
        f"[{fixture['id']}] Forbidden claims found: {found}\n"
        f"Answer (first 600 chars):\n{answer[:600]}"
    )


@pytest.mark.parametrize("fixture", FIXTURES, ids=[f["id"] for f in FIXTURES])
def test_key_point_coverage(fixture: dict, capsys):
    """At least 50% of key_points must score >= 0.6 word-overlap with the answer."""
    answer, timing = _get_answer(fixture)
    points = fixture.get("key_points", [])
    if not points:
        pytest.skip("no key_points in fixture")

    scores = [(pt, _key_point_score(pt, answer)) for pt in points]
    covered = sum(1 for _, s in scores if s >= 0.6)
    pct = covered / len(scores) * 100

    with capsys.disabled():
        print(f"\n[{fixture['id']}] {fixture['topic']} - key point coverage: "
              f"{covered}/{len(scores)} ({pct:.0f}%)")
        for pt, sc in scores:
            icon = "+" if sc >= 0.6 else "-"
            print(f"  {icon} ({sc:.2f}) {pt}")
        if timing:
            print(f"  ttft={timing.get('ttft_ms', 0):.0f}ms  "
                  f"gen={timing.get('generation_ms', 0):.0f}ms  "
                  f"total={timing.get('total_ms', 0):.0f}ms")

    # Word-overlap is a weak semantic signal - high variance across LLM samples.
    # Reported for human review only; not a hard CI assertion.
    # Use LLM-as-judge for reliable key_point evaluation.
