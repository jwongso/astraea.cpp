#!/usr/bin/env python3
"""Run the golden suite against a live nz_tenancy binary and produce a JSON
report comparing what the configured LLM actually saw (anchor + chunks +
guidance) to what the golden fixture's must_include / required_sections /
key_points expect.

Output: report.json next to this script, loadable by the bundled web frontend
(static/index.html). The frontend never talks to the live service; it consumes
the JSON only.

Usage:

    # capture a fresh report
    python tools/eval_must_include/run_eval.py

    # narrow to one fixture for debugging
    python tools/eval_must_include/run_eval.py --only g001

    # capture, then immediately serve the frontend
    python tools/eval_must_include/run_eval.py --serve

Env vars:
    NZ_TENANCY_URL    base URL of the running service (default http://localhost:8001)
    NZ_TENANCY_TOKEN  X-API-Key value; falls back to systemd unit file if unset
    GOLDEN_DIR        override for tests/integration/golden/
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from typing import Any

import requests

# Local imports — sources.py and analyze.py live next to this file.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sources  # noqa: E402


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

REPO       = pathlib.Path(__file__).resolve().parent.parent.parent
GOLDEN_DIR = pathlib.Path(os.environ.get(
    "GOLDEN_DIR", REPO / "tests" / "integration" / "golden"))
OUT_DIR    = pathlib.Path(__file__).resolve().parent
OUT_FILE   = OUT_DIR / "report.json"

NZ_URL = os.environ.get("NZ_TENANCY_URL", "http://localhost:8001")
NO_LOG = {"X-No-Log": "1"}

# Fixture-source choices: keep the keys stable; the frontend keys badges off them.
SOURCE_CHOICES = ("repo", "python", "all")


def _token_from_systemd() -> str:
    """Match tests/integration/test_golden_quality.py's auth fallback."""
    unit = pathlib.Path.home() / ".config/systemd/user/astraea-nz-tenancy.service"
    try:
        m = re.search(r"Environment=PUBLIC_TOKEN=(\S+)", unit.read_text())
        return m.group(1) if m else ""
    except OSError:
        return ""


NZ_TOKEN = os.environ.get("NZ_TENANCY_TOKEN") or _token_from_systemd()


# Same minimal stoplist used by tests/integration/test_golden_quality.py.
# Word-overlap is a weak semantic signal but matches the existing convention
# in this repo, so the numbers here are directly comparable to the pytest
# scores. Anything stronger (embeddings / LLM-as-judge) is a follow-up.
STOP_WORDS = {
    "a", "an", "the", "and", "or", "but", "is", "are", "was", "were",
    "be", "been", "being", "have", "has", "had", "do", "does", "did",
    "will", "would", "could", "should", "may", "might", "must", "shall",
    "not", "no", "so", "if", "in", "on", "at", "to", "for", "of", "with",
    "this", "that", "it", "its", "by", "from", "as", "than", "also",
    "any", "all", "more", "such", "under", "into", "about", "when",
    "only", "then", "here", "their", "they", "what", "which", "who",
}

# Coverage thresholds (word-overlap fraction in [0, 1]).
SUPPORTABLE_THRESHOLD = 0.40   # at least 40% of substantive tokens present in context
COVERED_THRESHOLD     = 0.60   # at least 60% present in the model's final answer


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _tokens(text: str) -> list[str]:
    return [w for w in re.findall(r"[a-z0-9]+", text.lower())
            if w not in STOP_WORDS and len(w) > 2]


def _overlap(point_tokens: list[str], haystack: str) -> float:
    if not point_tokens:
        return 1.0
    low = haystack.lower()
    return sum(1 for w in point_tokens if w in low) / len(point_tokens)


def _section_id_from_doc(doc_id: str, title: str = "") -> str:
    """Convert "NZLEG/RTA/s48" -> "s48".

    context_debug anchor sections use UUID document_ids, not path IDs.
    Fall back to parsing the title ("s48 Entry to premises" -> "s48").
    """
    if not doc_id:
        return ""
    last = doc_id.rsplit("/", 1)[-1]
    # UUID segment - not a section ID; parse title instead
    if len(last) > 20 and "-" in last:
        m = re.match(r"(s\d+[A-Z]{0,3}(?:\(\d+\))?)", title.strip(), re.IGNORECASE)
        return m.group(1).lower() if m else ""
    return last.strip()


def _section_regex(ref: str) -> re.Pattern[str]:
    """Match "s48" / "section 48" / "s 48" / "s48(5)" forms in prose.

    Mirrors tests/integration/test_golden_quality.py:_section_regex so the
    "required_cited_in_answer" delta here lines up exactly with the pytest
    suite's hard-pass list.
    """
    m = re.match(r"s(\d+)(.*)", ref)
    if not m:
        return re.compile(re.escape(ref), re.IGNORECASE)
    num    = m.group(1)
    suffix = re.escape(m.group(2))
    return re.compile(rf"(?:s\s*{num}|[Ss]ection\s+{num}){suffix}", re.IGNORECASE)


def _git_commit() -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL, text=True).strip()
        return out
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


# ---------------------------------------------------------------------------
# LLM-name discovery
# ---------------------------------------------------------------------------
#
# The eval frontend, the markdown report, and the per-fixture commentary all
# need to refer to the *actual* model under test (qwen3, llama-3.1, gpt-oss,
# whatever the binary is configured with). Hardcoding "Qwen" makes the tool
# model-specific. Discovery order, first hit wins:
#
#   1. --llm-name CLI flag
#   2. LLM_NAME env var (operator override)
#   3. GET <NZ_TENANCY_URL>/healthz, find the "llm" check's URL, then
#      GET <that-url>/models (OpenAI-compatible probe used by llama-server,
#      vLLM, SGLang, TGI, etc.) and take the first model id.
#   4. GET ${LLM_BASE_URL}/models directly if env is set.
#   5. LLM_MODEL env var (the model name string the binary was launched with).
#   6. Literal fallback: "the LLM".
#
# Network failures at any step are non-fatal — the eval continues with the
# next fallback. The discovered value (and its origin) is logged once and
# embedded in report.meta so every downstream view (frontend, markdown,
# per-fixture LLM digests) can substitute the same string consistently.

def _probe_models_endpoint(base_url: str) -> str | None:
    """GET {base_url}/models and return the first model id, or None.

    Accepts urls with or without a trailing `/models`. Times out fast — this
    runs once per invocation and is best-effort.
    """
    if not base_url:
        return None
    url = base_url.rstrip("/")
    if not url.endswith("/models"):
        url = url + "/models"
    try:
        r = requests.get(url, timeout=4)
        if r.status_code != 200:
            return None
        data = r.json()
    except (requests.RequestException, ValueError):
        return None
    # OpenAI shape: {"object":"list","data":[{"id":"qwen3",...}, ...]}
    if isinstance(data, dict):
        items = data.get("data") or data.get("models") or []
        if items and isinstance(items[0], dict):
            mid = items[0].get("id") or items[0].get("name")
            if mid:
                return str(mid)
    return None


def _discover_llm_name(*, override: str | None) -> tuple[str, str]:
    """Return (name, origin) where origin is a short provenance string."""

    if override:
        return override, "--llm-name flag"

    env_name = os.environ.get("LLM_NAME")
    if env_name:
        return env_name, "LLM_NAME env"

    # Probe service /healthz to find the configured LLM url.
    try:
        r = requests.get(f"{NZ_URL.rstrip('/')}/healthz", timeout=4)
        if r.status_code == 200:
            data = r.json()
            for chk in data.get("checks", []) or []:
                if chk.get("name") == "llm" and chk.get("url"):
                    discovered = _probe_models_endpoint(chk["url"])
                    if discovered:
                        return discovered, f"{chk['url']} /models"
    except (requests.RequestException, ValueError):
        pass

    # Try a direct probe of LLM_BASE_URL when the binary exports it.
    base = os.environ.get("LLM_BASE_URL", "")
    if base:
        discovered = _probe_models_endpoint(base)
        if discovered:
            return discovered, f"{base}/models"

    env_model = os.environ.get("LLM_MODEL")
    if env_model:
        return env_model, "LLM_MODEL env"

    return "the LLM", "fallback (no source available)"


# ---------------------------------------------------------------------------
# SSE capture
# ---------------------------------------------------------------------------

def _stream_one(question: str) -> dict[str, Any]:
    """POST /ask/stream with feedback_context:true and capture every event.

    Returns a dict with keys: answer, sources, context_debug, timing.
    Raises on HTTP error.
    """
    headers = {"X-API-Key": NZ_TOKEN, "Content-Type": "application/json", **NO_LOG}
    payload = {"question": question, "feedback_context": True}

    r = requests.post(f"{NZ_URL}/ask/stream", json=payload,
                      headers=headers, stream=True, timeout=180)
    if r.status_code != 200:
        raise RuntimeError(
            f"/ask/stream returned {r.status_code}: {r.text[:300]}")

    tokens: list[str] = []
    sources: dict[str, Any] | None = None
    context_debug: dict[str, Any] | None = None
    timing: dict[str, Any] | None = None

    # SSE: lines come as either "event: NAME" or "data: PAYLOAD" or blank.
    # Drogon's `sources` frame uses `event: sources\ndata: {...}`; all the
    # other event types are typed by a top-level `type` field inside the
    # JSON payload itself (so they look like plain `data: {...}` frames).
    current_event = ""
    for raw in r.iter_lines():
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", errors="replace")
        line = raw.strip()
        if not line:
            current_event = ""
            continue
        if line.startswith("event:"):
            current_event = line[6:].strip()
            continue
        if not line.startswith("data:"):
            continue
        payload_str = line[5:].lstrip()
        if payload_str == "[DONE]":
            break
        try:
            ev = json.loads(payload_str)
        except json.JSONDecodeError:
            continue

        if current_event == "sources" or ("sources" in ev and "type" not in ev):
            sources = ev
            current_event = ""
            continue

        kind = ev.get("type", "")
        if kind == "token":
            tokens.append(ev.get("text", ""))
        elif kind == "context_debug":
            context_debug = ev
        elif kind == "timing":
            timing = ev

    return {
        "answer":        "".join(tokens),
        "sources":       sources,
        "context_debug": context_debug,
        "timing":        timing,
    }


# ---------------------------------------------------------------------------
# Delta computation
# ---------------------------------------------------------------------------

def _build_actual(stream: dict[str, Any]) -> dict[str, Any]:
    """Flatten the captured streams into a frontend-friendly shape.

    All shape choices here are mirrored in static/app.js's renderers.
    """
    cd = stream.get("context_debug") or {}
    anchor = cd.get("anchor") or {}
    raw_sections = anchor.get("sections") or []
    sections = []
    for s in raw_sections:
        doc_id = s.get("document_id", "")
        title  = s.get("title", "")
        sections.append({
            "document_id": doc_id,
            "section_id":  _section_id_from_doc(doc_id, title),
            "title":       title,
            "tokens":      s.get("tokens", 0),
            "preview":     s.get("preview", ""),
        })

    chunks = []
    for c in (cd.get("chunks") or []):
        chunks.append({
            "document_id": c.get("document_id", ""),
            "date":        c.get("date", ""),
            "score":       c.get("score", 0.0),
            "passed_gate": c.get("passed_gate", True),
            "tokens":      c.get("tokens", 0),
            "preview":     c.get("preview", ""),
            "full_text":   c.get("full_text", ""),
        })

    routing = cd.get("statute_routing") or {}
    return {
        "answer":           stream.get("answer", ""),
        "rewritten_query":  cd.get("rewritten_query", ""),
        "rewrite_used":     cd.get("rewrite_used", False),
        "matched_intents":  routing.get("matched_intents", []),
        "dominant_route":   routing.get("dominant_route", ""),
        "anchor": {
            "route_matched": anchor.get("route_matched", False),
            "method":        anchor.get("method", ""),
            "max_hits":      anchor.get("max_hits", 0),
            "sections":      sections,
        },
        "guidance":   cd.get("guidance") or {},
        "chunks":     chunks,
        "budget":     cd.get("budget") or {},
        "timing":     stream.get("timing") or {},
        "sources":    stream.get("sources") or {},
    }


def _key_point_eval(point: str, context_blob: str, answer: str) -> dict[str, Any]:
    tokens = _tokens(point)
    context_overlap = _overlap(tokens, context_blob)
    answer_overlap  = _overlap(tokens, answer)
    supportable = context_overlap >= SUPPORTABLE_THRESHOLD
    covered     = answer_overlap  >= COVERED_THRESHOLD
    if covered and supportable:
        verdict = "ok"
    elif covered and not supportable:
        # answer mentions the key concepts but the context didn't carry them:
        # either the model paraphrased loosely, or it relied on prior weights
        # rather than the retrieved evidence. Flag for inspection.
        verdict = "covered_without_evidence"
    elif supportable and not covered:
        verdict = "missed_in_answer"
    else:
        verdict = "unsupportable"
    return {
        "text":                     point,
        "tokens":                   tokens,
        "context_overlap":          round(context_overlap, 3),
        "answer_overlap":           round(answer_overlap, 3),
        "supportable_from_context": supportable,
        "covered_in_answer":        covered,
        "verdict":                  verdict,
    }


def _parent_satisfies(req: str, retrieved_set: set[str]) -> bool:
    """A retrieved parent section satisfies a subsection requirement.

    Retrieved "s24" satisfies required "s24(1)(e)" because the parent chunk
    contains the subsection text. This mirrors the corpus-side decision in
    src/anchor.cpp to index at parent-section granularity.
    Comparison is case-insensitive (s49B == s49b).
    """
    req_low = req.lower()
    if req_low in retrieved_set:
        return True
    parent = re.sub(r"\(.*$", "", req_low)
    return bool(parent) and parent in retrieved_set


def _citation_substrate_eval(citation: dict[str, Any],
                             anchor_sections: list[dict[str, Any]]
                             ) -> dict[str, Any]:
    """section_faithfulness asset check.

    The golden citation declares (section, key_rule). If the anchor delivers
    a chunk for that section but the 600-char preview doesn't carry the
    key_rule's substantive tokens, the model has the section ID but not the
    substrate — exactly the truncation symptom from PROBLEM.md.
    """
    section = citation.get("section", "") or ""
    key_rule = citation.get("key_rule", "") or ""
    retrieved_set = {s["section_id"] for s in anchor_sections if s["section_id"]}

    section_present = _parent_satisfies(section, retrieved_set)

    # Locate the anchor chunk(s) that satisfy this citation (exact or parent).
    parent = re.sub(r"\(.*$", "", section)
    matching = [s for s in anchor_sections
                if s["section_id"] == section or s["section_id"] == parent]
    matched_preview = "\n\n".join(s.get("preview", "") for s in matching)

    rule_tokens = _tokens(key_rule)
    rule_overlap = _overlap(rule_tokens, matched_preview)
    substrate_present = section_present and rule_overlap >= SUPPORTABLE_THRESHOLD

    if not section_present:
        verdict = "section_missing"
    elif not substrate_present:
        # Section retrieved but key_rule text not in the preview the LLM saw.
        # Almost always: 600-char anchor cap truncated the operative clause.
        verdict = "section_present_substrate_truncated"
    else:
        verdict = "ok"

    return {
        "section":            section,
        "title":              citation.get("title", "") or "",
        "url":                citation.get("url", "") or "",
        "key_rule":           key_rule,
        "section_present":    section_present,
        "matched_section_ids": [s["section_id"] for s in matching],
        "rule_overlap":       round(rule_overlap, 3),
        "substrate_present":  substrate_present,
        "verdict":            verdict,
    }


def _delta(golden: dict[str, Any], actual: dict[str, Any]) -> dict[str, Any]:
    answer     = actual.get("answer", "")
    answer_low = answer.lower()
    required   = list(golden.get("required_sections", []) or [])
    retrieved  = [s["section_id"].lower() for s in actual["anchor"]["sections"]
                  if s["section_id"]]
    retrieved_set = set(retrieved)

    # --- section_recall ---------------------------------------------------
    sections_present = [s for s in required if _parent_satisfies(s, retrieved_set)]
    sections_missing = [s for s in required if not _parent_satisfies(s, retrieved_set)]
    extra_retrieved  = [s for s in retrieved if s not in required]
    required_cited_in_answer = [
        s for s in required if _section_regex(s).search(answer)
    ]
    # Sections the LLM *cited* but never retrieved: by definition a hallucination
    # (or model prior-knowledge leak), regardless of whether the citation is
    # legally correct.
    cited_in_answer_set: set[str] = set()
    # Heuristic: scan the answer for "sNN" tokens
    for m in re.finditer(r"\bs(\d+)([A-Za-z]?)\b", answer):
        cited_in_answer_set.add(f"s{m.group(1)}{m.group(2)}")
    sections_cited_but_not_retrieved = sorted(
        s for s in cited_in_answer_set
        if not _parent_satisfies(s, retrieved_set))

    # --- Assemble the substrate blob (what the LLM actually had to lean on) --
    blob_parts = [s["preview"] for s in actual["anchor"]["sections"]]
    blob_parts += [c["full_text"] or c["preview"] for c in actual["chunks"]]
    g = actual.get("guidance") or {}
    if g.get("injected"):
        blob_parts.append(g.get("court_name", "") or "")
    context_blob = "\n\n".join(p for p in blob_parts if p)
    context_blob_low = context_blob.lower()

    # --- must_include (key_points) ----------------------------------------
    points = golden.get("key_points", []) or []
    key_points = [_key_point_eval(p, context_blob, answer) for p in points]
    supportable_count = sum(1 for k in key_points if k["supportable_from_context"])
    covered_count     = sum(1 for k in key_points if k["covered_in_answer"])
    unsupportable     = [k for k in key_points if not k["supportable_from_context"]]

    # --- no_violation -----------------------------------------------------
    # Two probes per forbidden phrase:
    #   1. Does it appear in the *context*? Means the retrieval served the LLM
    #      something it could plausibly parrot. This is a leading indicator.
    #   2. Does it appear in the *answer*? Hard failure.
    forbidden = list(golden.get("forbidden_claims", []) or [])
    forbidden_assets = []
    for c in forbidden:
        cl = c.lower()
        forbidden_assets.append({
            "phrase":     c,
            "in_context": cl in context_blob_low,
            "in_answer":  cl in answer_low,
        })
    forbidden_in_answer  = [a["phrase"] for a in forbidden_assets if a["in_answer"]]
    forbidden_in_context = [a["phrase"] for a in forbidden_assets if a["in_context"]]

    # --- section_faithfulness (citation key_rule substrate) ---------------
    cites = ((golden.get("citations") or {}).get("legislation") or [])
    citations = [_citation_substrate_eval(c, actual["anchor"]["sections"])
                 for c in cites]
    citations_ok          = sum(1 for c in citations if c["verdict"] == "ok")
    citations_truncated   = sum(1 for c in citations
                                if c["verdict"] == "section_present_substrate_truncated")
    citations_missing     = sum(1 for c in citations
                                if c["verdict"] == "section_missing")

    # --- accuracy (lightweight, golden vs actual answer token agreement) --
    golden_answer = golden.get("answer", "") or ""
    g_toks = set(_tokens(golden_answer))
    a_toks = set(_tokens(answer))
    answer_token_jaccard = (len(g_toks & a_toks) / len(g_toks | a_toks)
                            if (g_toks | a_toks) else 0.0)

    # --- Overall verdict, conservative ------------------------------------
    if sections_missing or forbidden_in_answer or sections_cited_but_not_retrieved:
        verdict = "bad"
    elif citations_truncated or unsupportable or forbidden_in_context:
        # Retrieval pulled the right sections but the substrate is incomplete,
        # OR the context contains a poison phrase. Either way the answer
        # cannot fulfil must_include/no_violation without rolling the model
        # weights for support.
        verdict = "context_gap"
    elif covered_count < len(key_points):
        verdict = "answer_gap"
    else:
        verdict = "ok"

    return {
        "section_recall": {
            "required":                         required,
            "retrieved":                        retrieved,
            "sections_present":                 sections_present,
            "sections_missing":                 sections_missing,
            "extra_sections_retrieved":         extra_retrieved,
            "required_cited_in_answer":         required_cited_in_answer,
            "sections_cited_but_not_retrieved": sections_cited_but_not_retrieved,
        },
        "must_include": {
            "key_points": key_points,
            "summary": {
                "total":         len(key_points),
                "supportable":   supportable_count,
                "covered":       covered_count,
                "unsupportable": len(unsupportable),
            },
        },
        "no_violation": {
            "forbidden":            forbidden_assets,
            "forbidden_in_answer":  forbidden_in_answer,
            "forbidden_in_context": forbidden_in_context,
        },
        "section_faithfulness": {
            "citations": citations,
            "summary": {
                "total":      len(citations),
                "ok":         citations_ok,
                "truncated":  citations_truncated,
                "missing":    citations_missing,
            },
        },
        "accuracy": {
            "answer_token_jaccard": round(answer_token_jaccard, 3),
        },
        "context_blob_chars": len(context_blob),
        "verdict":            verdict,
    }


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def _load_fixtures(*, source: str, only: str | None) -> list[dict[str, Any]]:
    """Load + normalise fixtures via sources.py.

    See sources.py module docstring for the schema shapes that get unified.
    """
    if source == "repo":
        fixtures = sources.load_repo_golden(GOLDEN_DIR)
    elif source == "python":
        fixtures = sources.load_python_eval()
    elif source == "all":
        fixtures = sources.load_all(golden_dir=GOLDEN_DIR)
    else:
        sys.exit(f"unknown --source {source!r}; pick one of {SOURCE_CHOICES}")

    if not fixtures:
        hint = (f"\n  hint: for --source python, set ASTRAEA_PY_ROOT "
                f"(currently {sources.py_root()})") if source != "repo" else ""
        sys.exit(f"no fixtures found for --source {source}{hint}")

    if only:
        narrowed = [f for f in fixtures
                    if f["id"] == only or f["id"].endswith(only)]
        if not narrowed:
            sys.exit(f"no fixture matches --only {only!r}")
        fixtures = narrowed
    return fixtures


def _run(fixture: dict[str, Any]) -> dict[str, Any]:
    t0 = time.time()
    try:
        stream = _stream_one(fixture["question"])
    except Exception as e:                   # noqa: BLE001
        return {
            "id":       fixture["id"],
            "topic":    fixture.get("topic", ""),
            "source":   fixture.get("source", ""),
            "question": fixture.get("question", ""),
            "error":    f"{type(e).__name__}: {e}",
        }
    actual = _build_actual(stream)
    delta  = _delta(fixture["golden"], actual)
    return {
        "id":               fixture["id"],
        "source":           fixture.get("source", ""),
        "key_point_origin": fixture.get("key_point_origin", ""),
        "topic":            fixture.get("topic", ""),
        "question":         fixture["question"],
        "golden":           fixture["golden"],
        "actual":           actual,
        "delta":            delta,
        "wall_ms":          round((time.time() - t0) * 1000, 1),
        "path":             fixture.get("path", ""),
    }


def _summarise(records: list[dict[str, Any]]) -> dict[str, Any]:
    by_verdict: dict[str, int] = {"ok": 0, "context_gap": 0, "answer_gap": 0, "bad": 0}
    n_err = 0
    for r in records:
        if "error" in r:
            n_err += 1
            continue
        v = r.get("delta", {}).get("verdict", "")
        if v in by_verdict:
            by_verdict[v] += 1

    def _sum(path: list[str]) -> int:
        total = 0
        for r in records:
            d: Any = r.get("delta", {})
            for p in path:
                d = (d or {}).get(p, {}) if isinstance(d, dict) else 0
            if isinstance(d, list):
                total += len(d)
            elif isinstance(d, (int, float)):
                total += int(d)
        return total

    return {
        "fixtures":    len(records),
        "errors":      n_err,
        **by_verdict,
        "section_recall": {
            "required_total":                    _sum(["section_recall", "required"]),
            "retrieved_total":                   _sum(["section_recall", "retrieved"]),
            "missing":                           _sum(["section_recall", "sections_missing"]),
            "cited_in_answer":                   _sum(["section_recall", "required_cited_in_answer"]),
            "cited_but_not_retrieved":           _sum(["section_recall", "sections_cited_but_not_retrieved"]),
        },
        "must_include": {
            "total":         _sum(["must_include", "summary", "total"]),
            "supportable":   _sum(["must_include", "summary", "supportable"]),
            "covered":       _sum(["must_include", "summary", "covered"]),
            "unsupportable": _sum(["must_include", "summary", "unsupportable"]),
        },
        "no_violation": {
            "in_answer":  _sum(["no_violation", "forbidden_in_answer"]),
            "in_context": _sum(["no_violation", "forbidden_in_context"]),
        },
        "section_faithfulness": {
            "total":     _sum(["section_faithfulness", "summary", "total"]),
            "ok":        _sum(["section_faithfulness", "summary", "ok"]),
            "truncated": _sum(["section_faithfulness", "summary", "truncated"]),
            "missing":   _sum(["section_faithfulness", "summary", "missing"]),
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", choices=SOURCE_CHOICES, default="repo",
                    help="fixture source: repo (tests/integration/golden/*.json), "
                         "python (~/proj/priv/astraea/.training/splits/test.jsonl + oracle overlay), "
                         "or all (union, dedup by id). Default: repo.")
    ap.add_argument("--py-root", type=pathlib.Path,
                    help="path to the Python astraea repo "
                         "(overrides ASTRAEA_PY_ROOT; default: ~/proj/priv/astraea)")
    ap.add_argument("--only",  metavar="ID",
                    help="run a single fixture by id (e.g. g001 or a Facebook post_id)")
    ap.add_argument("--out",   type=pathlib.Path, default=OUT_FILE,
                    help=f"output JSON path (default {OUT_FILE})")
    ap.add_argument("--serve", action="store_true",
                    help="after writing report.json, start the static frontend server")
    ap.add_argument("--port",  type=int, default=8765,
                    help="port for --serve (default 8765)")
    ap.add_argument("--llm-name", default=None,
                    help="override the LLM display name; otherwise discovered "
                         "via /healthz -> /v1/models, LLM_BASE_URL/models, "
                         "or LLM_MODEL env (default: auto-detect)")
    args = ap.parse_args()

    if args.py_root:
        os.environ["ASTRAEA_PY_ROOT"] = str(args.py_root)

    if not NZ_TOKEN:
        print("warning: NZ_TENANCY_TOKEN unset and no systemd unit found; "
              "service may reject the request with 401", file=sys.stderr)

    llm_name, llm_origin = _discover_llm_name(override=args.llm_name)
    print(f"[eval] LLM name: {llm_name!r}  (origin: {llm_origin})",
          file=sys.stderr)

    fixtures = _load_fixtures(source=args.source, only=args.only)
    by_source: dict[str, int] = {}
    for fx in fixtures:
        by_source[fx.get("source", "?")] = by_source.get(fx.get("source", "?"), 0) + 1
    print(f"[eval] {len(fixtures)} fixture(s) "
          f"({', '.join(f'{k}={v}' for k,v in by_source.items())})",
          file=sys.stderr)
    print(f"[eval] target {NZ_URL}", file=sys.stderr)

    def _write_partial(recs: list[dict[str, Any]], done: int, total: int) -> None:
        partial = {
            "meta": {
                "ts":          dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
                "url":         NZ_URL,
                "git_commit":  _git_commit(),
                "source":      args.source,
                "golden_dir":  str(GOLDEN_DIR),
                "py_root":     str(sources.py_root()),
                "by_source":   by_source,
                "llm_name":    llm_name,
                "llm_origin":  llm_origin,
                "partial":     {"done": done, "total": total},
                "thresholds":  {"supportable": SUPPORTABLE_THRESHOLD,
                                "covered":     COVERED_THRESHOLD},
            },
            "summary":  _summarise(recs),
            "fixtures": recs,
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(partial, indent=2, ensure_ascii=False))

    records: list[dict[str, Any]] = []
    for i, fx in enumerate(fixtures, 1):
        print(f"[eval] {i}/{len(fixtures)} {fx['id']} {fx.get('topic','')} ...",
              end=" ", file=sys.stderr, flush=True)
        rec = _run(fx)
        if "error" in rec:
            print(f"ERROR {rec['error']}", file=sys.stderr)
        else:
            v = rec["delta"]["verdict"]
            kp = rec["delta"]["must_include"]["summary"]
            sf = rec["delta"]["section_faithfulness"]["summary"]
            print(f"{v:>12}  kp {kp['covered']}/{kp['total']} cov "
                  f"{kp['supportable']}/{kp['total']} supp  "
                  f"cite {sf['ok']}/{sf['total']} ok ({sf['truncated']} trunc)",
                  file=sys.stderr)
        records.append(rec)
        _write_partial(records, i, len(fixtures))

    report = {
        "meta": {
            "ts":          dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
            "url":         NZ_URL,
            "git_commit":  _git_commit(),
            "source":      args.source,
            "golden_dir":  str(GOLDEN_DIR),
            "py_root":     str(sources.py_root()),
            "by_source":   by_source,
            "llm_name":    llm_name,
            "llm_origin":  llm_origin,
            "thresholds": {
                "supportable": SUPPORTABLE_THRESHOLD,
                "covered":     COVERED_THRESHOLD,
            },
        },
        "summary":  _summarise(records),
        "fixtures": records,
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(f"[eval] wrote {args.out} ({args.out.stat().st_size} bytes)",
          file=sys.stderr)

    # Also drop a markdown analysis next to it — the LLM-friendly view.
    # Keeps the two outputs in sync per run; the frontend offers a "Copy as
    # markdown for LLM" button that consumes the same renderer in-browser.
    try:
        sys.path.insert(0, str(pathlib.Path(__file__).parent))
        import analyze  # noqa: E402
        md = analyze.render(report, only=None, patterns_only=False)
        md_path = args.out.with_suffix(".md")
        md_path.write_text(md)
        print(f"[eval] wrote {md_path} ({len(md)} chars)", file=sys.stderr)
    except Exception as e:                   # noqa: BLE001
        print(f"[eval] warning: could not produce markdown report: {e}",
              file=sys.stderr)

    if args.serve:
        from serve import serve  # noqa: E402
        serve(args.port, OUT_DIR)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
