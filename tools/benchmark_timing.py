"""End-to-end timing benchmark for the astraea /ask/stream pipeline.

Runs N questions through the API, collects the `timing` SSE event from each,
and reports per-slot statistics: min, mean, p50, p95, p99, max (milliseconds).

Usage:
    python .training/benchmark_timing.py
    python .training/benchmark_timing.py --url http://localhost:8001 --n 10
    python .training/benchmark_timing.py --out .training/benchmark_results.json

Run from the astraea project root. Requires the service to be running.
All requests include X-No-Log: 1 to avoid polluting the live question log.
"""

import argparse
import json
import statistics
import subprocess
import time
from pathlib import Path

import httpx


def _git_info(repo_path: str | None = None) -> dict:
    """Return commit hash and subject for the repo at repo_path (or cwd)."""
    try:
        def _run(args):
            return subprocess.check_output(
                args, cwd=repo_path, stderr=subprocess.DEVNULL
            ).decode().strip()
        return {
            "commit": _run(["git", "rev-parse", "HEAD"]),
            "short":  _run(["git", "rev-parse", "--short", "HEAD"]),
            "subject": _run(["git", "log", "-1", "--format=%s"]),
        }
    except Exception:
        return {"commit": "unknown", "short": "unknown", "subject": "unknown"}

DEFAULT_URL = "http://127.0.0.1:8001"
API_KEY = "Oqt3jfJtpY89VYVpVZITG-obkOd-cmgS"

TIMING_SLOTS = [
    "sanitize_ms",
    "route_ms",
    "embed_ms",
    "qdrant_ms",
    "anchor_ms",
    "guidance_ms",
    "context_assembly_ms",
    "llm_wait_ms",
    "ttft_ms",
    "generation_ms",
    "total_ms",
]

QUESTIONS = [
    "What is the maximum bond a landlord can charge for a residential tenancy?",
    "My landlord has not returned my bond after I moved out three weeks ago. What can I do?",
    "Can my landlord enter the property without giving me 24 hours notice?",
    "What are the healthy homes heating requirements for rental properties?",
    "I have mould in my rental and my landlord refuses to fix it. What are my rights?",
    "My landlord wants to increase the rent. How much notice do they need to give me?",
    "Can a landlord charge a pet bond in New Zealand?",
    "What happens if my landlord does not lodge my bond with Tenancy Services?",
    "I received a 90-day notice to vacate. Is this legal and what should I do?",
    "My hot water cylinder has broken and my landlord says I need to wait two weeks. Is that acceptable?",
]


def _ask_stream(client: httpx.Client, url: str, question: str) -> dict | None:
    timing = None
    try:
        with client.stream(
            "POST",
            f"{url}/ask/stream",
            json={"question": question},
            headers={
                "Content-Type": "application/json",
                "X-No-Log": "1",
                "X-API-Key": API_KEY,
            },
            timeout=180.0,
        ) as resp:
            resp.raise_for_status()
            for raw in resp.iter_lines():
                if not raw.startswith("data: "):
                    continue
                try:
                    evt = json.loads(raw[6:])
                except json.JSONDecodeError:
                    continue
                if evt.get("type") == "timing":
                    timing = evt
                    break
    except Exception as exc:
        print(f"  ERROR: {exc}")
    return timing


def _percentile(data: list[float], p: float) -> float:
    if not data:
        return 0.0
    data = sorted(data)
    idx = (len(data) - 1) * p / 100
    lo = int(idx)
    hi = lo + 1
    if hi >= len(data):
        return data[lo]
    return data[lo] + (data[hi] - data[lo]) * (idx - lo)


def _fmt(v: float) -> str:
    return f"{v:8.1f}"


def run(url: str, n: int, out: str | None, repo: str | None = None) -> None:
    questions = (QUESTIONS * ((n // len(QUESTIONS)) + 1))[:n]

    git = _git_info(repo)
    print(f"Benchmarking {url} with {n} request(s)...")
    print(f"Commit: {git['short']} - {git['subject']}\n")

    results: list[dict] = []
    with httpx.Client() as client:
        for i, q in enumerate(questions, 1):
            print(f"[{i}/{n}] {q[:70]}")
            t_wall = time.perf_counter()
            timing = _ask_stream(client, url, q)
            elapsed = time.perf_counter() - t_wall
            if timing is None:
                print(f"  no timing event received (wall={elapsed:.1f}s)")
                continue
            results.append(timing)
            print(f"  total={timing.get('total_ms', 0):.0f}ms  "
                  f"embed={timing.get('embed_ms', 0):.0f}ms  "
                  f"gen={timing.get('generation_ms', 0):.0f}ms  "
                  f"ttft={timing.get('ttft_ms', 0):.0f}ms")

    if not results:
        print("\nNo results collected.")
        return

    print(f"\n{'Slot':<22} {'min':>8} {'mean':>8} {'p50':>8} {'p95':>8} {'p99':>8} {'max':>8}")
    print("-" * 76)

    slot_data: dict[str, list[float]] = {s: [] for s in TIMING_SLOTS}
    for r in results:
        for s in TIMING_SLOTS:
            v = r.get(s)
            if isinstance(v, (int, float)):
                slot_data[s].append(float(v))

    summary: dict[str, dict] = {}
    for slot, vals in slot_data.items():
        if not vals:
            continue
        stats = {
            "min": min(vals),
            "mean": statistics.mean(vals),
            "p50": _percentile(vals, 50),
            "p95": _percentile(vals, 95),
            "p99": _percentile(vals, 99),
            "max": max(vals),
            "n": len(vals),
        }
        summary[slot] = stats
        print(f"  {slot:<20} "
              f"{_fmt(stats['min'])} "
              f"{_fmt(stats['mean'])} "
              f"{_fmt(stats['p50'])} "
              f"{_fmt(stats['p95'])} "
              f"{_fmt(stats['p99'])} "
              f"{_fmt(stats['max'])}")

    print(f"\n  n={len(results)} request(s), all times in ms")

    if out:
        payload = {
            "url": url,
            "n": len(results),
            "git": git,
            "summary": summary,
            "raw": results,
        }
        Path(out).write_text(json.dumps(payload, indent=2))
        print(f"  results written to {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Timing benchmark for astraea /ask/stream")
    parser.add_argument("--url", default=DEFAULT_URL, help="Base URL of the API")
    parser.add_argument("--n", type=int, default=5, help="Number of requests to run")
    parser.add_argument("--out", default=None, help="Optional path to write JSON results")
    parser.add_argument("--repo", default=None,
                        help="Path to the C++ repo for git info (default: cwd)")
    args = parser.parse_args()
    run(args.url, args.n, args.out, args.repo)
