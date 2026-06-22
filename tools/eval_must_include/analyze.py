#!/usr/bin/env python3
"""LLM-friendly analyzer over report.json from run_eval.py.

Produces a compact markdown report (report.md by default) that lists ONLY
the deltas — not the raw retrieval assets — so it fits easily into an LLM
context window. Adds cross-fixture pattern detection on top: which section
IDs are repeatedly missing or truncated, which forbidden phrases keep
appearing in retrieval context, which topics dominate the failure modes.

Usage:
    # full markdown report next to report.json
    python tools/eval_must_include/analyze.py

    # just one fixture, to stdout
    python tools/eval_must_include/analyze.py --only g001

    # only the cross-fixture patterns (no per-fixture detail)
    python tools/eval_must_include/analyze.py --patterns-only
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent


# ---------------------------------------------------------------------------
# Per-fixture markdown
# ---------------------------------------------------------------------------

def _trunc(s: str, n: int = 160) -> str:
    s = " ".join((s or "").split())
    return s if len(s) <= n else s[: n - 1] + "…"


def _fixture_block(rec: dict[str, Any]) -> str:
    if "error" in rec:
        return (f"## {rec['id']} {rec.get('topic','')} [ERROR]\n\n"
                f"`{rec['error']}`\n")

    d = rec.get("delta", {})
    v = d.get("verdict", "?")
    sr = d.get("section_recall", {}) or {}
    sf = d.get("section_faithfulness", {}) or {}
    mi = d.get("must_include", {}) or {}
    nv = d.get("no_violation", {}) or {}

    lines: list[str] = []
    lines.append(f"## {rec['id']} {rec.get('topic','')} [{v}]")
    lines.append("")
    src = rec.get("source", "")
    origin = rec.get("key_point_origin", "")
    if src or origin:
        bits = []
        if src:    bits.append(f"source: {src}")
        if origin: bits.append(f"key_points: {origin}")
        lines.append(f"_{' · '.join(bits)}_")
        lines.append("")
    lines.append(f"**Q:** {_trunc(rec.get('question',''), 220)}")
    lines.append("")

    # --- section_recall ---------------------------------------------------
    req = sr.get("required", []) or []
    missing = sr.get("sections_missing", []) or []
    halluc = sr.get("sections_cited_but_not_retrieved", []) or []
    cited = set(sr.get("required_cited_in_answer", []) or [])
    if req:
        flags = []
        if missing:
            flags.append(f"MISSING from anchor: {', '.join(missing)}")
        not_cited = [s for s in req if s not in cited and s not in missing]
        if not_cited:
            flags.append(f"retrieved but NOT cited in answer: {', '.join(not_cited)}")
        if halluc:
            flags.append(f"HALLUCINATED in answer (cited but not retrieved): {', '.join(halluc)}")
        if not flags:
            flags.append("ok")
        lines.append(f"- **section_recall**: required={req} → {'; '.join(flags)}")
    else:
        lines.append("- **section_recall**: (no required_sections declared)")

    # --- section_faithfulness --------------------------------------------
    cites = sf.get("citations", []) or []
    if cites:
        sub_summary = sf.get("summary", {}) or {}
        sub_problems = [c for c in cites if c.get("verdict") != "ok"]
        head = (f"- **section_faithfulness**: "
                f"{sub_summary.get('ok',0)}/{sub_summary.get('total',0)} ok"
                f"  ({sub_summary.get('truncated',0)} truncated, "
                f"{sub_summary.get('missing',0)} missing)")
        lines.append(head)
        for c in sub_problems:
            verdict_short = {
                "section_present_substrate_truncated": "TRUNCATED",
                "section_missing":                     "MISSING",
            }.get(c.get("verdict", ""), c.get("verdict", "?"))
            lines.append(
                f"    - {verdict_short} {c.get('section','')}: "
                f"\"{_trunc(c.get('key_rule',''), 110)}\" "
                f"(overlap={c.get('rule_overlap',0)})")
    else:
        lines.append("- **section_faithfulness**: (no citations declared)")

    # --- must_include -----------------------------------------------------
    summary = mi.get("summary", {}) or {}
    pts = mi.get("key_points", []) or []
    if pts:
        head = (f"- **must_include**: "
                f"{summary.get('covered',0)}/{summary.get('total',0)} covered, "
                f"{summary.get('supportable',0)}/{summary.get('total',0)} supportable")
        if summary.get("unsupportable", 0):
            head += f", **{summary['unsupportable']} unsupportable**"
        lines.append(head)
        for p in pts:
            if p.get("verdict") == "ok":
                continue
            tag = {
                "missed_in_answer":          "MISSED IN ANSWER",
                "covered_without_evidence":  "COVERED WITHOUT EVIDENCE",
                "unsupportable":             "UNSUPPORTABLE",
            }.get(p.get("verdict", ""), p.get("verdict", "?"))
            lines.append(
                f"    - {tag}: \"{_trunc(p.get('text',''), 140)}\" "
                f"(ctx={p.get('context_overlap',0)}, ans={p.get('answer_overlap',0)})")
    else:
        lines.append("- **must_include**: (no key_points declared)")

    # --- no_violation -----------------------------------------------------
    forb = nv.get("forbidden", []) or []
    if forb:
        in_ans = nv.get("forbidden_in_answer", []) or []
        in_ctx = nv.get("forbidden_in_context", []) or []
        if not in_ans and not in_ctx:
            lines.append(f"- **no_violation**: 0/{len(forb)} hits — clean")
        else:
            lines.append(f"- **no_violation**: "
                         f"{len(in_ans)} in answer, {len(in_ctx)} in context")
            for c in in_ans:
                lines.append(f"    - VIOLATION (in answer): \"{c}\"")
            for c in in_ctx:
                if c in in_ans:
                    continue
                lines.append(f"    - POISON RISK (in context): \"{c}\"")
    else:
        lines.append("- **no_violation**: (no forbidden_claims declared)")

    # --- accuracy / context size ------------------------------------------
    acc = d.get("accuracy", {}) or {}
    lines.append(f"- **accuracy**: answer↔golden token jaccard = {acc.get('answer_token_jaccard',0)}")
    lines.append(f"- **context_blob_chars**: {d.get('context_blob_chars', 0)}")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Cross-fixture patterns
# ---------------------------------------------------------------------------

def _patterns(records: list[dict[str, Any]]) -> str:
    """Aggregate signals that point at where to invest first."""

    missing_sections    = collections.Counter()
    truncated_sections  = collections.Counter()
    halluc_sections     = collections.Counter()
    poison_phrases      = collections.Counter()
    unsupportable_topics = collections.Counter()
    truncated_examples: dict[str, str] = {}

    for r in records:
        if "error" in r:
            continue
        d = r.get("delta", {}) or {}
        topic = r.get("topic", "")

        for s in (d.get("section_recall", {}) or {}).get("sections_missing", []) or []:
            missing_sections[s] += 1
        for s in (d.get("section_recall", {}) or {}).get("sections_cited_but_not_retrieved", []) or []:
            halluc_sections[s] += 1

        for c in (d.get("section_faithfulness", {}) or {}).get("citations", []) or []:
            if c.get("verdict") == "section_present_substrate_truncated":
                truncated_sections[c.get("section", "")] += 1
                truncated_examples.setdefault(
                    c.get("section", ""),
                    _trunc(c.get("key_rule", ""), 110))

        for c in (d.get("no_violation", {}) or {}).get("forbidden_in_context", []) or []:
            poison_phrases[c] += 1

        unsup = (d.get("must_include", {}) or {}).get("summary", {}).get("unsupportable", 0)
        if unsup:
            unsupportable_topics[topic] += unsup

    out: list[str] = ["## Patterns across fixtures", ""]

    def _table(title: str, counter: collections.Counter, label: str,
               extras: dict[str, str] | None = None) -> None:
        if not counter:
            out.append(f"- {title}: none")
            return
        out.append(f"### {title}")
        out.append("")
        out.append(f"| {label} | count | example |")
        out.append("|---|---:|---|")
        for k, v in counter.most_common():
            ex = (extras or {}).get(k, "")
            out.append(f"| `{k}` | {v} | {ex} |")
        out.append("")

    _table("Sections repeatedly MISSING from anchor (raise route forced_sections or vector recall)",
           missing_sections, "section")
    _table("Sections repeatedly TRUNCATED in anchor (raise 600-char cap, or fetch chunks 0+1)",
           truncated_sections, "section", truncated_examples)
    _table("Sections cited by model but never retrieved (HALLUCINATED — strong signal of a routing gap)",
           halluc_sections, "section")
    _table("Forbidden phrases appearing in retrieved context (poison risk to investigate)",
           poison_phrases, "phrase")

    if unsupportable_topics:
        out.append("### Topics with the most unsupportable key_points")
        out.append("")
        out.append("| topic | unsupportable key_points (sum) |")
        out.append("|---|---:|")
        for k, v in unsupportable_topics.most_common():
            out.append(f"| {k} | {v} |")
        out.append("")
    else:
        out.append("- Topics with unsupportable key_points: none")

    return "\n".join(out)


# ---------------------------------------------------------------------------
# Top-level render
# ---------------------------------------------------------------------------

def render(report: dict[str, Any], *, only: str | None,
           patterns_only: bool) -> str:
    meta = report.get("meta", {}) or {}
    summary = report.get("summary", {}) or {}
    fixtures = report.get("fixtures", []) or []

    if only:
        fixtures = [f for f in fixtures if f.get("id") == only]
        if not fixtures:
            return f"# no fixture matches --only {only!r}\n"

    out: list[str] = []
    out.append("# astraea.cpp eval delta — markdown analysis")
    out.append("")
    out.append(f"- generated: `{meta.get('ts','')}`")
    out.append(f"- url: `{meta.get('url','')}`")
    out.append(f"- commit: `{meta.get('git_commit','')}`")
    if meta.get("llm_name"):
        origin = meta.get("llm_origin", "")
        out.append(f"- LLM under test: `{meta['llm_name']}`"
                   + (f"  _(detected via {origin})_" if origin else ""))
    if meta.get("source"):
        by_src = meta.get("by_source", {}) or {}
        bs = ", ".join(f"{k}={v}" for k, v in by_src.items()) if by_src else ""
        out.append(f"- fixture source: `{meta['source']}`"
                   + (f" ({bs})" if bs else ""))
    if meta.get("thresholds"):
        t = meta["thresholds"]
        out.append(f"- thresholds: supportable ≥ {t.get('supportable')}, covered ≥ {t.get('covered')}")
    out.append("")

    # Verdict mix
    n = summary.get("fixtures", 0) or 1
    def _pct(k): return f"{summary.get(k,0)}/{n} ({round(100*summary.get(k,0)/n)}%)"
    out.append("## Verdict mix")
    out.append("")
    out.append(f"- **ok**: {_pct('ok')}")
    out.append(f"- **answer_gap**: {_pct('answer_gap')}  _(context could have supported every key_point; model didn't surface them all)_")
    out.append(f"- **context_gap**: {_pct('context_gap')}  _(anchor truncation / unsupportable key_point / poison phrase in context)_")
    out.append(f"- **bad**: {_pct('bad')}  _(missing required section, forbidden phrase in answer, or hallucinated citation)_")
    if summary.get("errors", 0):
        out.append(f"- **errors**: {summary['errors']}")
    out.append("")

    # Dimension rollups
    sr = summary.get("section_recall", {}) or {}
    mi = summary.get("must_include", {}) or {}
    sf = summary.get("section_faithfulness", {}) or {}
    nv = summary.get("no_violation", {}) or {}
    out.append("## Dimension rollups")
    out.append("")
    out.append("| dimension | metric | value |")
    out.append("|---|---|---:|")
    out.append(f"| section_recall | required total | {sr.get('required_total',0)} |")
    out.append(f"| section_recall | missing | {sr.get('missing',0)} |")
    out.append(f"| section_recall | required cited in answer | {sr.get('cited_in_answer',0)} |")
    out.append(f"| section_recall | cited but not retrieved (hallucinated) | {sr.get('cited_but_not_retrieved',0)} |")
    out.append(f"| section_faithfulness | citations total | {sf.get('total',0)} |")
    out.append(f"| section_faithfulness | ok | {sf.get('ok',0)} |")
    out.append(f"| section_faithfulness | truncated | {sf.get('truncated',0)} |")
    out.append(f"| section_faithfulness | missing | {sf.get('missing',0)} |")
    out.append(f"| must_include | key_points total | {mi.get('total',0)} |")
    out.append(f"| must_include | supportable | {mi.get('supportable',0)} |")
    out.append(f"| must_include | covered | {mi.get('covered',0)} |")
    out.append(f"| must_include | unsupportable | {mi.get('unsupportable',0)} |")
    out.append(f"| no_violation | hits in answer | {nv.get('in_answer',0)} |")
    out.append(f"| no_violation | hits in context (poison risk) | {nv.get('in_context',0)} |")
    out.append("")

    out.append(_patterns(report.get("fixtures", []) or []))
    out.append("")

    if not patterns_only:
        out.append("## Per-fixture deltas")
        out.append("")
        for r in fixtures:
            out.append(_fixture_block(r))

    out.append("---")
    out.append("")
    out.append("_Generated by `tools/eval_must_include/analyze.py`. "
               "All deltas are deterministic text/word-overlap diffs; no LLM is in the loop._")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in",  dest="in_path",  type=pathlib.Path,
                    default=HERE / "report.json")
    ap.add_argument("--out", dest="out_path", type=pathlib.Path,
                    default=HERE / "report.md",
                    help="markdown output path; use - for stdout")
    ap.add_argument("--only", metavar="ID",
                    help="render only one fixture by id (also forces stdout)")
    ap.add_argument("--patterns-only", action="store_true",
                    help="skip per-fixture blocks; emit only the cross-fixture patterns")
    args = ap.parse_args()

    if not args.in_path.exists():
        print(f"error: {args.in_path} not found — run run_eval.py first",
              file=sys.stderr)
        return 2

    report = json.loads(args.in_path.read_text())
    md = render(report, only=args.only, patterns_only=args.patterns_only)

    if args.only or str(args.out_path) == "-":
        sys.stdout.write(md)
    else:
        args.out_path.write_text(md)
        print(f"[analyze] wrote {args.out_path} ({len(md)} chars, "
              f"{md.count(chr(10))} lines)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
