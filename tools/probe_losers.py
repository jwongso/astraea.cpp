#!/usr/bin/env python3
"""Probe the running nz_tenancy binary for the full routing+anchor decision
on specific eval questions.

This is the ONLY supported route diagnostic. Lessons learned the hard way:

  - You MUST probe with the EXACT eval-test question text. Paraphrases
    silently change which routes fire (PR #83 misdiagnosis: paraphrased
    Q2865 missed the pet_bond co-fire that was the actual derailer).
    The script refuses --question/--text input and only accepts --pids,
    loading full question text from .training/splits/test.jsonl.

  - You MUST verify the running binary is built from current source.
    With multiple build folders (build-dev / build-prod) and a long-
    running service that does NOT reload on rebuild, it is trivially
    easy to probe a stale binary. The script compares source mtime vs
    binary mtime and the binary's source-routes hash (from /healthz)
    against the local routes.cpp hash, and refuses to run on mismatch
    unless --i-know-what-im-doing is passed.

Output per loser includes:
  question_id, full_question (untruncated),
  matched_intents, dominant_route, trigger_terms, trigger_paths,
  forced_sections (union), per-route attribution from routes.cpp
  (forced_sections + leg_allow_list), effective union allow_list,
  retrieved sections diff vs baseline, judge violations, answer excerpt.

Usage:
    probe_losers.py --pids 2865632593607018 2886321248204819
    probe_losers.py --pids <pid> --baseline 2feaf8c --new b487c80
    probe_losers.py --pids <pid> --skip-staleness-check --i-know-what-im-doing
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import sys
import time
from typing import Any

# ---------------------------------------------------------------------------
# Defaults — match .training/eval_judge.py so we hit the same service
# ---------------------------------------------------------------------------

ASTRAEA_URL = os.environ.get("ASTRAEA_URL", "http://localhost:8001")
TOKEN       = os.environ.get("ASTRAEA_TOKEN",     "Oqt3jfJtpY89VYVpVZITG-obkOd-cmgS")
DEBUG_KEY   = os.environ.get("ASTRAEA_DEBUG_KEY", "nz2023")

_REPO        = pathlib.Path(__file__).resolve().parent.parent
ROUTES_CPP   = _REPO / "jurisdictions" / "nz_tenancy" / "routes.cpp"

_DEFAULT_PY  = pathlib.Path.home() / "proj/priv/astraea"
DEFAULT_TEST_JSONL = _DEFAULT_PY / ".training/splits/test.jsonl"
RESULTS_JSONL = _DEFAULT_PY / ".training/eval_results.jsonl"


# ---------------------------------------------------------------------------
# Question loading — by post_id, from splits/test.jsonl ONLY
# ---------------------------------------------------------------------------

def load_full_question(pid: str, test_jsonl: pathlib.Path) -> str:
    """Return the untruncated question text for `pid`, or raise."""
    if not test_jsonl.exists():
        raise SystemExit(
            f"ERROR: cannot find {test_jsonl}. Override via --test-jsonl.")
    for line in test_jsonl.read_text().splitlines():
        if not line.strip():
            continue
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            continue
        if d.get("post_id") == pid:
            q = d.get("question", "")
            if not q:
                raise SystemExit(f"ERROR: post_id {pid} has no 'question'.")
            return q
    raise SystemExit(f"ERROR: post_id {pid} not found in {test_jsonl}")


# ---------------------------------------------------------------------------
# Staleness checks — fail loudly when the running binary is older than the
# source file or when its routes hash disagrees with the local routes.cpp.
# ---------------------------------------------------------------------------

def _local_routes_hash() -> str:
    """sha1 of routes.cpp[:8], matching .training/eval_judge.py:_routes_hash."""
    return hashlib.sha1(ROUTES_CPP.read_bytes()).hexdigest()[:8]


def _binary_mtime_younger_than_source(binary_path: pathlib.Path) -> bool:
    if not binary_path.exists():
        return False
    return binary_path.stat().st_mtime >= ROUTES_CPP.stat().st_mtime


def _service_routes_hash_via_healthz() -> str | None:
    """Read the binary-reported routes hash from /healthz.

    Returns None on network error or non-200. A returned hash that differs
    from the local sha1(routes.cpp)[:8] means the running service was built
    from a different routes.cpp than what is on disk — the hard equality
    check landed in PR #85 (routes_hash exposed in /healthz)."""
    req = _import_requests()
    try:
        r = req.get(f"{ASTRAEA_URL}/healthz",
                    headers={"X-API-Key": TOKEN}, timeout=5)
        if r.status_code != 200:
            return None
        return r.json().get("routes_hash")
    except Exception:
        return None


def staleness_report(binary_path: pathlib.Path) -> list[str]:
    """Return a list of human-readable problems. Empty list = clean."""
    issues: list[str] = []
    if binary_path.exists() and not _binary_mtime_younger_than_source(binary_path):
        bin_mtime = time.strftime("%Y-%m-%d %H:%M",
                                  time.localtime(binary_path.stat().st_mtime))
        src_mtime = time.strftime("%Y-%m-%d %H:%M",
                                  time.localtime(ROUTES_CPP.stat().st_mtime))
        issues.append(
            f"BINARY STALE: {binary_path.name} mtime ({bin_mtime}) is "
            f"OLDER than routes.cpp ({src_mtime}). Rebuild before probing.")
    local_hash = _local_routes_hash()
    service_hash = _service_routes_hash_via_healthz()
    if service_hash and service_hash != local_hash:
        issues.append(
            f"ROUTES HASH MISMATCH: local routes.cpp={local_hash} "
            f"service /healthz reports={service_hash}. The running service "
            f"was built against a different routes.cpp.")
    return issues


# ---------------------------------------------------------------------------
# routes.cpp parser — for per-route attribution
# ---------------------------------------------------------------------------

def _parse_routes_for_attribution() -> dict[str, dict[str, list[str]]]:
    """{intent: {'forced_sections': [...], 'leg_allow_list': [...]}}."""
    text = ROUTES_CPP.read_text()
    text = re.sub(r"//[^\n]*", "", text)
    out: dict[str, dict[str, list[str]]] = {}
    intent_re = re.compile(r'\.intent\s*=\s*"([^"]+)"')
    string_re = re.compile(r'"([^"]*)"')

    intents = [(m.start(), m.group(1)) for m in intent_re.finditer(text)]
    lp_marker = text.find("LOW_PRIORITY_SECTIONS")
    if lp_marker != -1:
        intents = [(p, n) for p, n in intents if p < lp_marker]

    def _balanced(t: str, i: int) -> int:
        depth = 0
        n = len(t)
        while i < n:
            c = t[i]
            if c == '"':
                j = t.find('"', i + 1)
                if j == -1:
                    raise ValueError("unterminated string literal")
                i = j
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
        raise ValueError("unbalanced")

    for k, (pos, intent) in enumerate(intents):
        end = intents[k + 1][0] if k + 1 < len(intents) else len(text)
        chunk = text[pos:end]
        fields: dict[str, list[str]] = {"forced_sections": [], "leg_allow_list": []}
        for field in fields:
            m = re.search(rf"\.{field}\s*=\s*\{{", chunk)
            if not m:
                continue
            brace = chunk.index("{", m.end() - 1)
            close = _balanced(chunk, brace)
            body = chunk[brace + 1: close - 1]
            fields[field] = string_re.findall(body)
        out[intent] = fields
    return out


# ---------------------------------------------------------------------------
# SSE probe
# ---------------------------------------------------------------------------

def _import_requests():
    spec = importlib.util.find_spec("requests")
    if spec is None:
        sys.exit("ERROR: `pip install requests` required")
    return importlib.import_module("requests")


def probe(question: str) -> dict[str, Any] | None:
    """POST the exact question, collect SSE events, return context_debug."""
    req = _import_requests()
    payload = {
        "question": question,
        "debug_key": DEBUG_KEY,
        "eval_options": {"temperature": 0.0},
    }
    headers = {
        "X-API-Key": TOKEN,
        "X-No-Log": "1",
        "Content-Type": "application/json",
    }
    events: list[dict[str, Any]] = []
    with req.post(f"{ASTRAEA_URL}/ask/stream", json=payload, headers=headers,
                  stream=True, timeout=600) as r:
        r.raise_for_status()
        r.encoding = "utf-8"   # SSE Content-Type confuses requests
        for raw in r.iter_lines(decode_unicode=True):
            if not raw or not raw.startswith("data:"):
                continue
            body = raw[len("data:"):].strip()
            if body == "[DONE]":
                break
            try:
                events.append(json.loads(body))
            except json.JSONDecodeError:
                continue
    for ev in events:
        if ev.get("type") == "context_debug":
            return ev
    print(f"WARN: no context_debug event in {len(events)} events", file=sys.stderr)
    return None


# ---------------------------------------------------------------------------
# Eval-results loader
# ---------------------------------------------------------------------------

def _load_eval(commit_prefix: str) -> dict[str, dict[str, Any]]:
    if not commit_prefix:
        return {}
    out: dict[str, dict[str, Any]] = {}
    if not RESULTS_JSONL.exists():
        return out
    for line in RESULTS_JSONL.read_text().splitlines():
        if not line.strip():
            continue
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            continue
        if d.get("eval_meta", {}).get("git_commit", "").startswith(commit_prefix):
            out[d["post_id"]] = d
    return out


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def _print_banner(out_fh) -> None:
    print("# probe_losers.py — exact-question diagnostic", file=out_fh)
    print(f"# routes.cpp sha1[:8] = {_local_routes_hash()}", file=out_fh)
    print(f"# service URL         = {ASTRAEA_URL}", file=out_fh)
    print("# Probes use untruncated text from .training/splits/test.jsonl",
          file=out_fh)
    print("# NO PARAPHRASES.\n", file=out_fh)


def _report(
    pid: str,
    full_question: str,
    baseline: dict[str, Any] | None,
    new: dict[str, Any] | None,
    probe_event: dict[str, Any] | None,
    attribution: dict[str, dict[str, list[str]]],
    out_fh,
) -> None:
    p = lambda *a, **k: print(*a, **k, file=out_fh)

    p("=" * 90)
    p(f"POST {pid}")
    p(f"Q (full, {len(full_question)} chars):")
    for line in full_question.splitlines():
        p(f"   {line}")
    p()

    if baseline and new:
        p("Scores: baseline -> new   (delta)")
        for f in ("must_include_score", "no_violation_score", "section_recall",
                  "section_faithfulness", "accuracy_score", "overall_score"):
            b = baseline["scores"].get(f); n = new["scores"].get(f)
            if isinstance(b, (int, float)) and isinstance(n, (int, float)):
                d = n - b
                sgn = "+" if d > 0 else ""
                p(f"  {f:26s} {b:6.1f} -> {n:6.1f}   ({sgn}{d:.1f})")
        p()

    if probe_event is None:
        p("(no probe data — service did not return context_debug)")
        return

    sr = probe_event.get("statute_routing", {}) or {}
    matched = sr.get("matched_routes", []) or []
    dominant = sr.get("dominant_route", "") or ""
    trigger_terms = sr.get("trigger_terms", []) or []
    trigger_paths = sr.get("trigger_paths", {}) or {}
    union_forced = sr.get("forced_sections", []) or []
    ignored = sr.get("ignored_routes", []) or []
    near_miss = sr.get("near_miss_routes", []) or []

    p(f"matched_intents  ({len(matched)}): {matched}")
    p(f"dominant_route   : {dominant}")
    p(f"trigger_terms    : {trigger_terms}")
    p(f"trigger_paths    : {trigger_paths}")
    p(f"forced_sections  (union, {len(union_forced)}): {union_forced}")
    if ignored:
        p("ignored_routes:")
        for r in ignored:
            p(f"  - {r.get('route')}: {r.get('reason')}")
    if near_miss:
        p("near_miss_routes:")
        for r in near_miss:
            p(f"  - {r.get('route')}: broad_matched={r.get('broad_matched')}")
    p()

    p("Per-matched-route attribution (source: routes.cpp):")
    union_allow: list[str] = []
    seen: set[str] = set()
    for intent in matched:
        info = attribution.get(intent)
        if not info:
            p(f"  - {intent}: (not found in routes.cpp parser)")
            continue
        forced = info["forced_sections"]
        allow  = info["leg_allow_list"]
        p(f"  - {intent}")
        p(f"      forced_sections : {forced}")
        p(f"      leg_allow_list  : {allow}")
        for s in allow:
            if s not in seen:
                seen.add(s); union_allow.append(s)
    p()
    p(f"Effective union allow_list ({len(union_allow)}): {union_allow}")
    p()

    anchor = probe_event.get("anchor", {}) or {}
    sections = anchor.get("sections", []) or []
    p(f"Anchor sections delivered to LLM ({len(sections)}):")
    for s in sections:
        title = s.get("title", "")
        did   = s.get("document_id", "")
        toks  = s.get("tokens", 0)
        p(f"  - {did:24s}  {toks:4d} tokens  {title[:60]}")
    p()

    if baseline and new:
        b_secs = [r["case_id"] for r in baseline.get("retrieved_legislation", [])]
        n_secs = [r["case_id"] for r in new.get("retrieved_legislation", [])]
        added   = [s for s in n_secs if s not in b_secs]
        removed = [s for s in b_secs if s not in n_secs]
        p("Retrieved diff (baseline vs new):")
        p(f"  baseline ({len(b_secs)}): {b_secs}")
        p(f"  new      ({len(n_secs)}): {n_secs}")
        p(f"  ADDED   under new: {added}")
        p(f"  REMOVED under new: {removed}")
        if added:
            p("ADDED-section attribution:")
            for s in added:
                contributors = [r for r in matched
                                if s in attribution.get(r, {}).get("leg_allow_list", [])
                                or s in attribution.get(r, {}).get("forced_sections", [])]
                p(f"  {s}: contributed by {contributors or '(vector hit, not in any matched.allow_list)'}")
        p()

    if new:
        viols = new["scores"].get("violations_found") or []
        if viols:
            p("Judge violations (new run):")
            for v in viols[:4]:
                if isinstance(v, dict):
                    p(f"  - {json.dumps(v, indent=None)[:400]}")
                else:
                    p(f"  - {str(v)[:400]}")
            p()
        ans = new.get("astraea_answer", "")
        if ans:
            p("Answer excerpt (new run):")
            preview = ans.replace("\n", "\n  ")[:1200]
            p(f"  {preview}")
            p()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--pids", nargs="+", required=True,
                   help="post_id list; questions loaded from "
                        ".training/splits/test.jsonl. PARAPHRASES NOT ACCEPTED.")
    p.add_argument("--baseline", default=None,
                   help="Commit SHA prefix for baseline scores (optional)")
    p.add_argument("--new", default=None, dest="new_",
                   help="Commit SHA prefix for new scores (optional)")
    p.add_argument("--out", default=None,
                   help="Write report to this file instead of stdout")
    p.add_argument("--binary",
                   default=str(_REPO / "build-prod" / "apps" / "nz_tenancy" / "nz_tenancy"),
                   help="Path to the binary to mtime-check (default build-prod)")
    p.add_argument("--test-jsonl", default=None,
                   help=f"Override .training/splits/test.jsonl path "
                        f"(default {DEFAULT_TEST_JSONL})")
    p.add_argument("--skip-staleness-check", action="store_true",
                   help="Skip the binary-mtime + routes-hash sanity checks. "
                        "Requires --i-know-what-im-doing.")
    p.add_argument("--i-know-what-im-doing", action="store_true",
                   help="Acknowledge staleness-check bypass. No-op without "
                        "--skip-staleness-check.")
    args = p.parse_args()

    test_jsonl = (pathlib.Path(args.test_jsonl) if args.test_jsonl
                  else DEFAULT_TEST_JSONL)

    out_fh = open(args.out, "w") if args.out else sys.stdout

    # Staleness checks BEFORE any probing — this is the whole point of the
    # upgrade. The previous probe harness produced false-confidence results
    # against a stale binary (Jun 21 23:22 build folder cache).
    if not args.skip_staleness_check:
        issues = staleness_report(pathlib.Path(args.binary))
        if issues:
            for s in issues:
                print(f"# {s}", file=sys.stderr)
            print("# refusing to probe. Rebuild or pass --skip-staleness-check "
                  "--i-know-what-im-doing.", file=sys.stderr)
            return 2
    elif not args.i_know_what_im_doing:
        print("# --skip-staleness-check requires --i-know-what-im-doing",
              file=sys.stderr)
        return 2
    else:
        print("# WARN: staleness checks SKIPPED (--skip-staleness-check). "
              "Results may not reflect the source you are reading.",
              file=sys.stderr)

    _print_banner(out_fh)
    if args.skip_staleness_check:
        print("# WARN: staleness checks SKIPPED. Trust at your own risk.",
              file=out_fh)
        print(file=out_fh)

    attribution = _parse_routes_for_attribution()
    print(f"# Parsed {len(attribution)} routes from routes.cpp", file=sys.stderr)

    base_idx = _load_eval(args.baseline) if args.baseline else {}
    new_idx  = _load_eval(args.new_) if args.new_ else {}

    try:
        for pid in args.pids:
            full_q = load_full_question(pid, test_jsonl)
            base = base_idx.get(pid) if args.baseline else None
            new  = new_idx.get(pid) if args.new_ else None
            print(f"# Probing {pid}", file=sys.stderr)
            t0 = time.time()
            ev = probe(full_q)
            print(f"# probe took {time.time() - t0:.1f}s", file=sys.stderr)
            _report(pid, full_q, base, new, ev, attribution, out_fh)
            print(file=out_fh)
    finally:
        if args.out:
            out_fh.close()
            print(f"# wrote report to {args.out}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
