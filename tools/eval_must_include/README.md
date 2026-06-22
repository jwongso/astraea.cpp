# eval_must_include — golden-vs-substrate delta viewer

A diagnostic tool that answers one question per golden fixture:

> Did the retrieval (anchor + chunks + guidance) actually give the LLM
> under test enough raw material to satisfy what the golden expects, or
> is the answer structurally forced to either omit or hallucinate?

No second LLM is involved. Every check is a deterministic text diff.
The tool also auto-detects which model is under test (qwen3, llama-3.1,
gpt-oss, anything OpenAI-API-compatible) and substitutes the name into
every renderer — it isn't pinned to any vendor.

## What it measures

For each fixture, the tool POSTs `/ask/stream` with
`feedback_context: true`, captures the `event: sources` frame, the
`context_debug` SSE event, the streamed tokens, and the final `timing`
event, and then computes:

| Dimension | Golden side | LLM-substrate side | Delta |
|---|---|---|---|
| `section_recall` | `required_sections` | `anchor.sections[*]` section-ID | exact set diff + parent satisfies subsection |
| `section_faithfulness` | `citations.legislation[*].key_rule` | preview text of the matching anchor section | word-overlap — flags "section retrieved but 600-char preview cut the rule" |
| `must_include` | `key_points` (free text) | full assembled context blob + model answer | per-point context overlap and answer overlap |
| `no_violation` | `forbidden_claims` | retrieved context + model answer | substring hit. Hit in context = poison risk; hit in answer = hard fail |
| `accuracy` (lightweight) | golden answer | model answer | token-set Jaccard |
| section hallucination | (implied) | section IDs cited in answer | flags `sNN` cited but not in anchor |

Per-fixture overall verdicts:

- **ok** — required retrieved, no forbidden hits, every key_point covered
- **answer_gap** — context could have supported every key_point but the model didn't surface them all
- **context_gap** — anchor missing a required section, anchor preview truncated the key_rule, an unsupportable key_point, OR a forbidden phrase sitting in the retrieved context (poison risk)
- **bad** — required section missing, forbidden phrase in the answer, or section cited that was never retrieved

## Fixture sources

The tool can load three fixture shapes and unify them into one schema
(see `sources.py`):

- `--source repo` (default) — `tests/integration/golden/g*.json`. Hand-curated,
  richest: `required_sections`, `forbidden_claims`, `key_points`,
  full `citations.legislation` with `key_rule`. Currently 2 fixtures.
- `--source python` — `~/proj/priv/astraea/.training/splits/test.jsonl` (50
  questions with `bruce_answer` reference) overlaid with
  `oracle_results_A.jsonl` (structured `must_include_hits/misses` for the
  15 judged). For un-judged questions, `key_points` are derived
  deterministically by sentence-splitting `bruce_answer` and dropping
  fragments with fewer than 5 substantive tokens. `required_sections` are
  extracted via section-reference regex over the same answer.
- `--source all` — union of the above, dedup by id.

Override the Python path with `--py-root PATH` or `ASTRAEA_PY_ROOT`.

## LLM-name detection

The display name of the model under test is auto-detected at startup,
in order:

1. `--llm-name NAME`
2. `LLM_NAME` env var
3. GET `<NZ_TENANCY_URL>/healthz`, find the `llm` check's URL, then
   GET `<that-url>/models` (OpenAI-compatible probe — works against
   llama-server, vLLM, SGLang, TGI, and similar). Take the first model id.
4. GET `${LLM_BASE_URL}/models` directly when the env var is set.
5. `LLM_MODEL` env var (the string the binary was launched with).
6. Literal fallback: `the LLM`.

The detected name lands in `report.meta.llm_name` and is consumed by
the frontend, the markdown analyzer, and per-fixture renderers so every
view shows the same label.

## Running it

The nz_tenancy binary must be running and reachable at `NZ_TENANCY_URL`
(default `http://localhost:8001`). The endpoint must accept
`{"feedback_context": true}` (already supported on `main`).

```sh
# from the repo root — default = 2 repo golden fixtures
python tools/eval_must_include/run_eval.py

# full 50-question Python eval set
python tools/eval_must_include/run_eval.py --source python

# everything (52 fixtures: 2 repo + 50 python)
python tools/eval_must_include/run_eval.py --source all

# open the viewer immediately
python tools/eval_must_include/run_eval.py --source all --serve

# narrow to one fixture while iterating (id or stem)
python tools/eval_must_include/run_eval.py --only g001
python tools/eval_must_include/run_eval.py --source python --only 2829435277226750

# override the model display name
python tools/eval_must_include/run_eval.py --llm-name llama-3.1-8b-instruct
LLM_NAME=qwen2.5-14b python tools/eval_must_include/run_eval.py
```

Override the service URL and auth token via env:

```sh
NZ_TENANCY_URL=http://localhost:8001 \
NZ_TENANCY_TOKEN=Oqt3... \
python tools/eval_must_include/run_eval.py --source all
```

If `NZ_TENANCY_TOKEN` is not set, the script reads it from
`~/.config/systemd/user/astraea-nz-tenancy.service` — same fallback as
`tests/integration/test_golden_quality.py` and `tools/probe_losers.py`.

## What the frontend shows

Header strip shows the detected LLM name, fixture source split, run
timestamp, and git commit. Below that, summary cards roll up the four
dimensions plus the verdict mix.

Each fixture row collapses to id + topic + source badge + status badges
(`sec n/m`, `kp n/m`, `cite n/m`, plus `violation` / `halluc cite` when
relevant). Click to expand into six sections:

1. **section_recall** — side-by-side: golden's required list (with miss/hit
   marks and "cited in answer" annotations) vs. the anchor's retrieved
   section IDs (required vs. extra).
2. **section_faithfulness** — one row per `citations.legislation[]`
   entry showing the section, title, the golden `key_rule` text, and the
   word-overlap of that key_rule against the anchor preview the LLM
   actually saw. **A truncated row here is direct evidence that the
   600-char anchor cap is hurting must_include.**
3. **must_include** — one row per key_point with two overlap bars: one
   against the retrieved context blob, one against the model answer.
   Verdicts: `ok`, `missed_in_answer`, `covered_without_evidence`
   (potential hallucination), `unsupportable` (model cannot satisfy
   without inventing).
4. **no_violation** — one row per forbidden phrase showing whether it
   appears in the retrieved context (poison risk) and whether it appears
   in the model answer (hard failure).
5. **retrieval assets** — the anchor section cards (required ones
   highlighted in green border, extras dashed), guidance card, all case
   chunks with scores. Click any card to expand its preview.
6. **answer comparison** — golden answer next to model answer,
   side-by-side, with required-section regex hits highlighted in green
   and forbidden phrases highlighted in red.

Filter bar at the top lets you hide verdicts you don't care about, plus
a substring search across id/topic/question. Two export buttons:
**📋 copy LLM report** (whole-run markdown, paste into any LLM for
triage) and **⬇ report.md** (download the file). Each expanded fixture
also has its own **📋 copy this fixture as markdown** button.

## Public deployment via Cloudflare Tunnel

The viewer is a pure static frontend (vanilla JS, no dependencies, no
build step) plus a `report.json` / `report.md` pair generated per eval
run. It never calls back to the nz_tenancy service from the browser, so
exposing it to the public internet only exposes the **eval data itself**
— never the running RAG endpoint.

This is the pattern used for the live deployment at
`https://tenancy-eval.localrun.ai`:

```sh
# 1. Generate the report (talks to the local nz_tenancy binary).
python tools/eval_must_include/run_eval.py --source all

# 2. Start the static server on a loopback port.
python tools/eval_must_include/serve.py --port 8765 --no-open

# 3. Point a cloudflared tunnel at it. Either ad-hoc:
cloudflared tunnel --url http://127.0.0.1:8765

# ...or via a named tunnel with a stable hostname in ~/.cloudflared/config.yml:
#
#   tunnel: tenancy-eval
#   credentials-file: /home/wdha/.cloudflared/<uuid>.json
#   ingress:
#     - hostname: tenancy-eval.localrun.ai
#       service: http://127.0.0.1:8765
#     - service: http_status:404
```

The server already sets:

- `Cache-Control: no-store` on `/report.json` and `/report.md`
  (always-fresh per request)
- `Cache-Control: public, max-age=60` on CSS/JS/HTML (Cloudflare can
  edge-cache short-lived; reports change per eval run so longer doesn't
  help)
- `X-Robots-Tag: noindex, nofollow` plus a matching `<meta name="robots">`
  in the HTML — eval data should not show up in search engines
- `Referrer-Policy: no-referrer` and `X-Content-Type-Options: nosniff`

For auth, put **Cloudflare Access** in front of the tunnel route. The
server itself is intentionally unauthenticated — putting another bearer
token in the static frontend would only obscure, not protect.

### Permalinks

The frontend reflects the open fixture into the URL hash, so any link
that includes a fixture id auto-expands and scrolls on load:

```
https://tenancy-eval.localrun.ai/#g001
https://tenancy-eval.localrun.ai/#2829435277226750
```

The 🔗 button in the filter bar copies the current URL with hash.

Hard-coded in `run_eval.py` at the top:

```python
SUPPORTABLE_THRESHOLD = 0.40   # context overlap required for "could cover"
COVERED_THRESHOLD     = 0.60   # answer overlap required for "actually covered"
```

These mirror the heuristic in `tests/integration/test_golden_quality.py`
so numbers here are directly comparable. They are *not* a substitute for
human judgement — the value here is the **delta**, not the absolute score.

## Live probe (probe.html) — one question, see everything

`index.html` shows the batch report. `probe.html` is for interactive
debugging: pick (or paste) **one** question, hit ▶ probe, watch the
retrieval substrate land then the answer stream, get the same delta
math run against the result.

The page splits into three regions:

1. **Top controls** — fixture dropdown (groups by source), question
   textarea, ▶ probe button. Cmd/Ctrl+Enter also fires.
2. **Two-column split** — left is the *golden* expectation panel
   (required_sections, key_points, forbidden_claims, citation key_rules
   with their full text); right is the *live* panel with five
   progressive stages that tick from ○ pending → ● streaming → ✓ done
   as the SSE events arrive:
     1. routing decision (matched intents, dominant route)
     2. legislation anchor cards (each card glows green when its
        section_id satisfies a `required_sections` entry from golden)
     3. case-law chunks + guidance
     4. answer streaming token-by-token with `required_section` and
        `forbidden_claim` highlights live
     5. timing breakdown when the timing event arrives
3. **Bottom delta** — populates after the stream ends with the full
   four-dimension diff (section_recall, section_faithfulness,
   must_include, no_violation), identical to what the static report
   shows.

The SSE flow exposed to the page is:
`event: sources` → `type: confidence` → `type: context_debug`
(arrives in milliseconds — this is what tells you what the LLM
*will* lean on, before generation even starts) → many `type: token`
frames → `type: timing` → end. After the stream the proxy emits one
synthetic `type: probe_complete` frame carrying `{actual, delta}` so
the frontend can render the delta with zero further round-trips.

Permalinks: `probe.html#g001` auto-selects that fixture on load.

### Why is this useful?

Iterating on routing changes or anchor-cap tuning normally means:
edit C++ → rebuild → restart binary → run all 50 fixtures via
`run_eval.py` → re-read the report. That's minutes per cycle.

With the probe page: edit C++ → rebuild → restart binary → click ▶ probe.
The full asset pile plus the delta lands in seconds for the one
question you care about. The static report stays available in another
tab for the cross-fixture pattern view.

### Server endpoints (added by serve.py)

- `GET  /config`  →  `{nz_url, llm_name, llm_origin, has_token, probe_enabled}`
  so the page can decide what to render before the user clicks anything.
- `POST /probe`   body `{question, golden}` → proxies to
  `${NZ_TENANCY_URL}/ask/stream` with `feedback_context:true`, forwards
  the SSE byte-for-byte, then emits one final `type: probe_complete`
  event with the server-side delta computation (same math as run_eval.py).

The proxy keeps the X-API-Key server-side so it's never exposed to the
browser — important when the page is fronted by a public tunnel.

## Files

```
tools/eval_must_include/
  run_eval.py        # batch driver: streams /ask/stream, detects LLM name,
                     # builds report.json + report.md
  analyze.py         # standalone markdown analyzer over report.json
                     # (also cross-fixture pattern detection)
  sources.py         # fixture loaders for repo / python / oracle shapes
  serve.py           # stdlib http.server with two extra endpoints:
                     #   GET /config   - boot probe for the frontend
                     #   POST /probe   - server-side SSE proxy to /ask/stream
                     #                   + post-stream delta computation
  static/
    index.html       # batch report viewer shell
    probe.html       # one-question live probe page
    style.css        # palette matches apps/nz_tenancy/frontend
    app.js           # report viewer (vanilla JS, no deps)
    probe.js         # live probe page (vanilla JS, no deps)
  report.json        # generated; gitignored
  report.md          # generated; gitignored
  README.md          # this file
```

## Limitations (read before you trust a verdict)

- **Word overlap is weak.** A key_point saying "60 days written notice"
  matches the answer "60 days notice" but not "two months notice" even
  when the law is the same. Use this tool to find structural gaps
  (missing sections, truncated key_rules, poison in context), not to
  settle whether a specific answer is good.
- **`--source python` key_points are derived,** not authored. For the 35
  test.jsonl questions that don't have an oracle row, key_points come
  from sentence-splitting `bruce_answer`. This is a reasonable proxy
  but not a substitute for hand-curated `g00*.json` fixtures. Treat
  unsupportable verdicts on Python-only fixtures as "investigate" rather
  than "definitely missing".
- **Anchor preview is 200 chars in `context_debug`** (one limit), while
  the prompt that actually reaches the LLM uses the 600-char cap from
  `src/anchor.cpp`. The substrate check here is therefore a *lower
  bound* on what the LLM saw — if the 200-char preview already passes,
  the prompt definitely had the substrate; if it fails, dig into the
  chunk text or raise the preview cap in `context_debug`.
- **Section IDs are extracted from `document_id` by splitting on `/`**
  (e.g. `NZLEG/RTA/s48` → `s48`). If a future jurisdiction uses a
  different convention, update `_section_id_from_doc` in `run_eval.py`.
- **The tool requires the running binary to be built from the source
  tree you're comparing against.** Unlike `tools/probe_losers.py`, it
  does not verify build freshness; if you're chasing a regression
  across commits, rebuild and restart the service before re-running.

