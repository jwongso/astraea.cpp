/* astraea.cpp eval delta — live probe page (vanilla JS).
 *
 * Single-question workflow:
 *   1. User picks a fixture (or types a custom question) → golden panel
 *      populates with required_sections / key_points / forbidden_claims /
 *      citation key_rules.
 *   2. User hits ▶ probe → POST to /probe (proxy in serve.py) which calls
 *      nz_tenancy's /ask/stream with feedback_context:true and forwards
 *      the SSE back to the browser verbatim.
 *   3. As the SSE arrives we progressively render:
 *        - routing decision (when context_debug arrives)
 *        - legislation anchor cards (highlighted green when they satisfy
 *          a required_sections entry from golden)
 *        - case-law chunks + guidance
 *        - tokens streaming into the answer pane
 *        - timing breakdown when the timing event arrives
 *   4. On stream completion the server emits a synthetic `probe_complete`
 *      event carrying the actual + delta computed by run_eval.py's same
 *      delta math. We render the full delta below.
 *
 * Some renderers are intentionally duplicated from app.js (dim*, escapeHtml,
 * etc.) to keep this page self-contained. If you change a renderer here,
 * sync it in app.js too — both consume the same delta shape from run_eval.py.
 */

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

let LLM_NAME    = "the LLM";
let CONFIG      = null;
let REPORT      = null;
let CURRENT_FX  = null;        // currently-loaded fixture object
let ABORTER     = null;        // AbortController for in-flight probe

const escapeHtml = (s) => (s ?? "").toString()
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");

function bar(value, klass = "") {
    const w = Math.min(100, Math.max(0, value * 100));
    return `<span class="bar ${klass}"><span style="width:${w}%"></span></span>`;
}

function badge(text, klass) {
    return `<span class="badge ${klass}">${escapeHtml(text)}</span>`;
}

function verdictBadge(v) {
    const map = { ok: "ok", answer_gap: "warn", context_gap: "warn", bad: "bad" };
    return badge(v, map[v] || "muted");
}

/* ------------------------------------------------------------------ */
/* CONFIG + REPORT load                                                */
/* ------------------------------------------------------------------ */

async function loadConfig() {
    try {
        const r = await fetch("/config", { cache: "no-store" });
        if (r.ok) CONFIG = await r.json();
    } catch { /* probe will fail later with a clearer message */ }
    LLM_NAME = (CONFIG && CONFIG.llm_name) || "the LLM";
}

async function loadReport() {
    try {
        const r = await fetch("report.json", { cache: "no-store" });
        if (r.ok) REPORT = await r.json();
    } catch { /* no report = no fixture picker, but custom questions still work */ }
}

function renderHeaderMeta() {
    const bits = [];
    if (LLM_NAME)             bits.push(`LLM: <code>${escapeHtml(LLM_NAME)}</code>`);
    if (CONFIG && CONFIG.nz_url) bits.push(`via <code>${escapeHtml(CONFIG.nz_url)}</code>`);
    if (CONFIG && CONFIG.llm_origin) bits.push(`<small>(${escapeHtml(CONFIG.llm_origin)})</small>`);
    $("#meta").innerHTML = bits.join(" &middot; ");
}

function populateFixturePicker() {
    const sel = $("#fixture-picker");
    if (!REPORT || !REPORT.fixtures) return;

    // Group by source so the dropdown is sortable / scannable.
    const groups = {};
    for (const f of REPORT.fixtures) {
        const src = f.source || "unknown";
        if (!groups[src]) groups[src] = [];
        groups[src].push(f);
    }
    for (const [src, items] of Object.entries(groups)) {
        const og = document.createElement("optgroup");
        og.label = `${src}  (${items.length})`;
        for (const f of items) {
            const label = `${f.id}  ${f.topic || ""} — ${(f.question || "").slice(0, 80)}`;
            const opt = document.createElement("option");
            opt.value = f.id;
            opt.textContent = label;
            og.appendChild(opt);
        }
        sel.appendChild(og);
    }
}

/* ------------------------------------------------------------------ */
/* GOLDEN panel                                                        */
/* ------------------------------------------------------------------ */

function renderGolden(fx) {
    const body = $("#golden-body");
    if (!fx) {
        body.innerHTML = `<div class="empty-state">
            No fixture selected — answer will still be probed, but the delta
            will be empty (no golden expectations to diff against).
        </div>`;
        return;
    }
    body.classList.remove("empty-state");
    const g = fx.golden || {};
    const req      = g.required_sections || [];
    const kps      = g.key_points || [];
    const forb     = g.forbidden_claims || [];
    const cites    = (g.citations && g.citations.legislation) || [];
    const origin   = fx.key_point_origin || "";
    const blocks = [];

    blocks.push(`<div class="golden-block">
        <h3>question</h3>
        <div class="question">${escapeHtml(fx.question || "")}</div>
    </div>`);

    blocks.push(`<div class="golden-block">
        <h3>required_sections  (${req.length})</h3>
        ${req.length === 0
            ? `<small style="color:var(--muted)">(none declared)</small>`
            : `<ul>${req.map(s => `<li><code>${escapeHtml(s)}</code></li>`).join("")}</ul>`}
    </div>`);

    blocks.push(`<div class="golden-block">
        <h3>key_points / must_include  (${kps.length})${origin ? ` <small style="color:var(--muted)">· ${escapeHtml(origin)}</small>` : ""}</h3>
        ${kps.length === 0
            ? `<small style="color:var(--muted)">(none declared)</small>`
            : `<ul>${kps.map(p => `<li>${escapeHtml(p)}</li>`).join("")}</ul>`}
    </div>`);

    blocks.push(`<div class="golden-block">
        <h3>forbidden_claims  (${forb.length})</h3>
        ${forb.length === 0
            ? `<small style="color:var(--muted)">(none declared)</small>`
            : `<ul>${forb.map(c => `<li style="color:var(--err-fg)">${escapeHtml(c)}</li>`).join("")}</ul>`}
    </div>`);

    blocks.push(`<div class="golden-block">
        <h3>citations.legislation  (${cites.length})</h3>
        ${cites.length === 0
            ? `<small style="color:var(--muted)">(none declared)</small>`
            : `<ul>${cites.map(c => `<li>
                <code>${escapeHtml(c.section || "")}</code>${escapeHtml(c.title || "")}
                ${c.key_rule ? `<span class="key-rule">${escapeHtml(c.key_rule)}</span>` : ""}
              </li>`).join("")}</ul>`}
    </div>`);

    body.innerHTML = blocks.join("");
}

/* ------------------------------------------------------------------ */
/* LIVE panel — progressive stages                                     */
/* ------------------------------------------------------------------ */

function _ensureLiveLayout() {
    const body = $("#live-body");
    body.classList.remove("empty-state");
    body.innerHTML = `
        <div class="live-stage" data-stage="routing">
            <h3><span class="icon">○</span> 1. routing</h3>
            <div class="stage-body">awaiting context_debug…</div>
        </div>
        <div class="live-stage" data-stage="anchor">
            <h3><span class="icon">○</span> 2. legislation anchor</h3>
            <div class="stage-body">awaiting context_debug…</div>
        </div>
        <div class="live-stage" data-stage="chunks">
            <h3><span class="icon">○</span> 3. case-law chunks &amp; guidance</h3>
            <div class="stage-body">awaiting context_debug…</div>
        </div>
        <div class="live-stage" data-stage="answer">
            <h3><span class="icon">○</span> 4. answer streaming</h3>
            <div class="stage-body"><div class="live-answer" id="live-answer"></div></div>
        </div>
        <div class="live-stage" data-stage="timing">
            <h3><span class="icon">○</span> 5. timing</h3>
            <div class="stage-body">awaiting timing event…</div>
        </div>`;
}

function _setStage(name, klass, headerExtra) {
    const el = $(`.live-stage[data-stage="${name}"]`);
    if (!el) return;
    el.classList.remove("active", "done", "error");
    if (klass) el.classList.add(klass);
    const icon = el.querySelector(".icon");
    if (icon) {
        icon.textContent = klass === "done" ? "✓"
                         : klass === "error" ? "✗"
                         : klass === "active" ? "●"
                         : "○";
    }
    if (headerExtra) {
        const h = el.querySelector("h3");
        let extra = h.querySelector(".extra");
        if (!extra) {
            extra = document.createElement("small");
            extra.className = "extra";
            extra.style.cssText = "margin-left:auto;color:var(--muted);font-weight:normal";
            h.appendChild(extra);
        }
        extra.innerHTML = headerExtra;
    }
}

function _stageBody(name) {
    return $(`.live-stage[data-stage="${name}"] .stage-body`);
}

function renderRouting(routing) {
    const intents = routing.matched_intents || [];
    const dom     = routing.dominant_route || "";
    const triggered = routing.triggered;
    if (!triggered && intents.length === 0) {
        _setStage("routing", "done", `<span class="badge muted">no route fired</span>`);
        _stageBody("routing").innerHTML =
            `<small style="color:var(--muted)">No route matched. Anchor will be vector-only.</small>`;
        return;
    }
    _setStage("routing", "done",
        `<span class="badge ok">${intents.length} intent(s)</span>`);
    _stageBody("routing").innerHTML = `
        <div><strong>matched_intents:</strong> ${intents.map(i => `<code>${escapeHtml(i)}</code>`).join(" ") || "(none)"}</div>
        ${dom ? `<div style="margin-top:4px"><strong>dominant_route:</strong> <code>${escapeHtml(dom)}</code></div>` : ""}
    `;
}

function renderAnchor(anchor, requiredSet) {
    const sections = anchor.sections || [];
    _setStage("anchor", sections.length ? "done" : "active",
        `<span class="badge ${sections.length ? "ok" : "warn"}">${sections.length} section(s)${anchor.route_matched ? " · route-driven" : " · degraded"}</span>`);
    const cards = sections.length === 0
        ? `<small style="color:var(--muted)">No legislation retrieved into anchor.</small>`
        : `<div class="live-assets">${sections.map(s => `
            <div class="live-asset ${requiredSet.has(s.section_id) ? "required-by-golden" : ""}" onclick="this.classList.toggle('expanded')">
                <div class="live-asset-id">${escapeHtml(s.section_id || s.document_id)}
                    <span class="live-asset-score">${s.tokens || 0} tok</span>
                    ${requiredSet.has(s.section_id) ? `<span class="live-asset-score" style="background:var(--ok-bg);color:var(--ok-fg)">required ✓</span>` : ""}
                </div>
                <div class="live-asset-title">${escapeHtml(s.title || "")}</div>
                <div class="live-asset-preview">${escapeHtml(s.preview || "(no preview)")}</div>
            </div>`).join("")}</div>`;
    _stageBody("anchor").innerHTML = cards;
}

function renderChunks(chunks, guidance) {
    const cards = [];
    if (guidance && guidance.injected) {
        cards.push(`<div class="live-asset" style="border-color:var(--accent)">
            <div class="live-asset-id">guidance: ${escapeHtml(guidance.source || "")}
                <span class="live-asset-score">${(guidance.score ?? 0).toFixed?.(3) ?? guidance.score}</span></div>
            <div class="live-asset-title">${escapeHtml(guidance.court_name || "")} (${escapeHtml(guidance.reason || "")})</div>
        </div>`);
    }
    for (const c of chunks) {
        cards.push(`<div class="live-asset" onclick="this.classList.toggle('expanded')">
            <div class="live-asset-id">${escapeHtml(c.document_id || "")}
                <span class="live-asset-score">${(c.score || 0).toFixed(3)}</span>
                <span class="live-asset-score">${c.tokens || 0} tok</span></div>
            <div class="live-asset-title">${escapeHtml(c.date || "")}</div>
            <div class="live-asset-preview">${escapeHtml(c.preview || c.full_text || "")}</div>
        </div>`);
    }
    _setStage("chunks", "done",
        `<span class="badge ${chunks.length ? "ok" : "warn"}">${chunks.length} chunk(s)${guidance && guidance.injected ? " + guidance" : ""}</span>`);
    _stageBody("chunks").innerHTML = cards.length
        ? `<div class="live-assets">${cards.join("")}</div>`
        : `<small style="color:var(--muted)">No chunks/guidance.</small>`;
}

function highlightAnswer(text, fx) {
    let out = escapeHtml(text);
    const req = (fx && fx.golden && fx.golden.required_sections) || [];
    for (const s of req) {
        const m = s.match(/^s(\d+)(.*)$/);
        if (!m) continue;
        const re = new RegExp(`\\b(s\\s*${m[1]}${m[2].replace(/[()]/g, "\\$&")}|[Ss]ection\\s+${m[1]}${m[2].replace(/[()]/g, "\\$&")})\\b`, "g");
        out = out.replace(re, x => `<mark class="req">${x}</mark>`);
    }
    for (const c of ((fx && fx.golden && fx.golden.forbidden_claims) || [])) {
        const re = new RegExp(c.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
        out = out.replace(re, x => `<mark class="forb">${x}</mark>`);
    }
    return out;
}

function appendToken(text, fx) {
    const el = $("#live-answer");
    if (!el) return;
    // Re-highlight the whole thing on each token. Fine at this scale (~200 tokens).
    el.dataset.raw = (el.dataset.raw || "") + text;
    el.innerHTML = highlightAnswer(el.dataset.raw, fx);
    el.scrollTop = el.scrollHeight;
    _setStage("answer", "active",
        `<span class="badge warn">${el.dataset.raw.length} chars</span>`);
}

function finalizeAnswerStage(charCount) {
    _setStage("answer", "done", `<span class="badge ok">${charCount} chars</span>`);
}

function renderTiming(timing) {
    const slots = [
        ["embed_ms",      "embed"],
        ["anchor_ms",     "anchor"],
        ["guidance_ms",   "guidance"],
        ["rewrite_ms",    "rewrite"],
        ["retrieve_ms",   "retrieve"],
        ["context_ms",    "context"],
        ["llm_wait_ms",   "llm wait"],
        ["ttft_ms",       "TTFT"],
        ["generation_ms", "generation"],
        ["total_ms",      "total"],
    ];
    const html = slots
        .filter(([k]) => k in timing)
        .map(([k, label]) => `<div class="slot">
            <span class="v">${Math.round(timing[k])}ms</span>
            <span class="k">${escapeHtml(label)}</span>
        </div>`).join("");
    _setStage("timing", "done",
        timing.total_ms != null ? `<span class="badge ok">${Math.round(timing.total_ms)}ms total</span>` : "");
    _stageBody("timing").innerHTML = `<div class="live-timing">${html}</div>`;
}

/* ------------------------------------------------------------------ */
/* DELTA panel — duplicates the dim* renderers from app.js              */
/* ------------------------------------------------------------------ */

function dimSectionRecall(d) {
    const sr = d.section_recall || {};
    const required  = sr.required || [];
    const retrieved = sr.retrieved || [];
    const missing   = new Set(sr.sections_missing || []);
    const cited     = new Set(sr.required_cited_in_answer || []);
    const halluc    = sr.sections_cited_but_not_retrieved || [];

    const lhs = required.length === 0
        ? `<li class="warn">(none required)</li>`
        : required.map(s => {
            const inAnswer = cited.has(s);
            const klass = missing.has(s) ? "miss" : "hit";
            const tag1 = missing.has(s) ? "missing from retrieval" : "in anchor";
            const tag2 = inAnswer ? "cited in answer" : "NOT cited in answer";
            const tag2Klass = inAnswer ? "" : "warn";
            return `<li class="${klass}">
                <code>${escapeHtml(s)}</code>
                <small style="color:var(--muted)"> · ${escapeHtml(tag1)}</small>
                <small class="${tag2Klass}"> · ${escapeHtml(tag2)}</small>
            </li>`;
        }).join("");
    const rhs = retrieved.length === 0
        ? `<li class="warn">(nothing retrieved into anchor)</li>`
        : retrieved.map(s => {
            const klass = required.includes(s) ? "hit" : "";
            const extra = required.includes(s) ? "required" : "extra";
            return `<li class="${klass}"><code>${escapeHtml(s)}</code>
                <small style="color:var(--muted)"> · ${escapeHtml(extra)}</small></li>`;
        }).join("");
    const halluc_html = halluc.length === 0 ? "" :
        `<div style="margin-top:8px;color:var(--err-fg);font-size:0.82rem">
            <strong>⚠ cited but not retrieved (likely hallucinated):</strong>
            ${halluc.map(s => `<code>${escapeHtml(s)}</code>`).join(", ")}
         </div>`;
    return `<div class="diff">
        <div class="pane"><h4>Golden — required_sections</h4><ul>${lhs}</ul></div>
        <div class="pane"><h4>Anchor — retrieved (${escapeHtml(LLM_NAME)} saw these)</h4><ul>${rhs}</ul></div>
    </div>${halluc_html}`;
}

function dimMustInclude(d) {
    const mi = d.must_include || {};
    const points = mi.key_points || [];
    if (!points.length) return `<div class="empty">no key_points in this fixture</div>`;
    const rows = points.map(p => {
        const klass =
            p.verdict === "ok" ? "ok" :
            p.verdict === "unsupportable" ? "bad" :
            p.verdict === "covered_without_evidence" ? "info" : "warn";
        const verdictLabel = {
            "ok":                       "covered + supportable",
            "missed_in_answer":         "supportable from context but missing from answer",
            "covered_without_evidence": "in answer but NO evidence in context (model prior knowledge / paraphrase)",
            "unsupportable":            "context cannot support this — answer cannot cover without hallucinating",
        }[p.verdict] || p.verdict;
        const cBarClass = p.supportable_from_context ? "ok" : "bad";
        const aBarClass = p.covered_in_answer        ? "ok" : "bad";
        return `<tr class="${klass}">
            <td>${escapeHtml(p.text)}</td>
            <td class="num">${bar(p.context_overlap, cBarClass)}${p.context_overlap}</td>
            <td class="num">${bar(p.answer_overlap, aBarClass)}${p.answer_overlap}</td>
            <td>${escapeHtml(verdictLabel)}</td>
        </tr>`;
    }).join("");
    return `<table class="kp-table">
        <thead><tr>
            <th>key_point (golden expects this in ${escapeHtml(LLM_NAME)}'s answer)</th>
            <th>overlap with retrieved context</th>
            <th>overlap with model answer</th>
            <th>verdict</th>
        </tr></thead>
        <tbody>${rows}</tbody>
    </table>`;
}

function dimSectionFaithfulness(d) {
    const sf = d.section_faithfulness || {};
    const cites = sf.citations || [];
    if (!cites.length) return `<div class="empty">no citations declared in golden</div>`;
    const rows = cites.map(c => {
        const klass = c.verdict === "ok" ? "ok" :
                      c.verdict === "section_missing" ? "bad" : "warn";
        const verdictLabel = {
            "ok":                                  "ok — key_rule substrate present in anchor preview",
            "section_present_substrate_truncated": "section retrieved BUT key_rule text not in 600-char preview (truncation)",
            "section_missing":                     "section not retrieved at all",
        }[c.verdict] || c.verdict;
        const overlapBarClass = c.rule_overlap >= 0.4 ? "ok" :
                                c.rule_overlap >= 0.2 ? "" : "bad";
        return `<tr class="${klass}">
            <td><code>${escapeHtml(c.section)}</code></td>
            <td>${escapeHtml(c.title)}</td>
            <td>${escapeHtml(c.key_rule)}</td>
            <td class="num">${bar(c.rule_overlap, overlapBarClass)}${c.rule_overlap}</td>
            <td>${escapeHtml(verdictLabel)}</td>
        </tr>`;
    }).join("");
    return `<table class="kp-table">
        <thead><tr>
            <th>section</th><th>title</th><th>key_rule (golden expects this)</th>
            <th>overlap with anchor preview</th><th>verdict</th>
        </tr></thead>
        <tbody>${rows}</tbody>
    </table>`;
}

function dimNoViolation(d) {
    const nv = d.no_violation || {};
    const items = nv.forbidden || [];
    if (!items.length) return `<div class="empty">no forbidden_claims declared</div>`;
    const rows = items.map(it => {
        const klass = it.in_answer ? "bad" : (it.in_context ? "warn" : "ok");
        let verdict;
        if (it.in_answer)       verdict = "VIOLATION — phrase appears in model answer";
        else if (it.in_context) verdict = "context contains this phrase (poison risk — model could parrot it)";
        else                    verdict = "absent from both context and answer";
        return `<tr class="${klass}">
            <td>${escapeHtml(it.phrase)}</td>
            <td>${it.in_context ? "✗ present" : "✓ absent"}</td>
            <td>${it.in_answer  ? "✗ present" : "✓ absent"}</td>
            <td>${escapeHtml(verdict)}</td>
        </tr>`;
    }).join("");
    return `<table class="kp-table">
        <thead><tr>
            <th>forbidden phrase</th><th>in retrieved context</th>
            <th>in model answer</th><th>verdict</th>
        </tr></thead>
        <tbody>${rows}</tbody>
    </table>`;
}

function renderDelta(delta, actual) {
    if (!delta) {
        $("#delta-body").innerHTML = `<div class="empty-state">
            No golden was attached to this probe (custom question) — nothing to diff against.
            The retrieval assets and model answer above still show what the LLM did.
        </div>`;
        return;
    }
    const v = delta.verdict || "?";
    $("#delta-body").classList.remove("empty-state");
    $("#delta-body").innerHTML = `
        <div class="delta-verdict ${v}">verdict: <strong>${escapeHtml(v)}</strong></div>
        <h3 style="font-size:0.78rem;text-transform:uppercase;letter-spacing:0.05em;color:var(--muted);margin:14px 0 6px">section_recall</h3>
        ${dimSectionRecall(delta)}
        <h3 style="font-size:0.78rem;text-transform:uppercase;letter-spacing:0.05em;color:var(--muted);margin:14px 0 6px">section_faithfulness</h3>
        ${dimSectionFaithfulness(delta)}
        <h3 style="font-size:0.78rem;text-transform:uppercase;letter-spacing:0.05em;color:var(--muted);margin:14px 0 6px">must_include</h3>
        ${dimMustInclude(delta)}
        <h3 style="font-size:0.78rem;text-transform:uppercase;letter-spacing:0.05em;color:var(--muted);margin:14px 0 6px">no_violation</h3>
        ${dimNoViolation(delta)}
    `;
}

/* ------------------------------------------------------------------ */
/* PROBE                                                               */
/* ------------------------------------------------------------------ */

function _setStatus(text, klass = "") {
    const el = $("#probe-status");
    el.className = "probe-status-text " + klass;
    el.textContent = text;
}

async function runProbe() {
    const question = $("#question-input").value.trim();
    if (!question) {
        _setStatus("type a question or pick a fixture", "error");
        return;
    }
    if (CONFIG && !CONFIG.probe_enabled) {
        _setStatus("server-side probe disabled — install python `requests` and ensure run_eval.py is importable", "error");
        return;
    }

    $("#probe-btn").disabled = true;
    $("#cancel-btn").classList.remove("hidden");
    _setStatus("connecting…", "streaming");
    _ensureLiveLayout();
    _setStage("routing",  "active");
    _setStage("anchor",   "active");
    _setStage("chunks",   "active");
    _setStage("answer",   "active");
    _setStage("timing",   "active");
    $("#delta-body").innerHTML = `<div class="empty-state">Delta will populate when the stream completes.</div>`;
    $("#delta-body").classList.add("empty-state");

    const golden = CURRENT_FX ? CURRENT_FX.golden : null;
    const requiredSet = new Set((golden && golden.required_sections) || []);

    ABORTER = new AbortController();
    let resp;
    try {
        resp = await fetch("/probe", {
            method:  "POST",
            headers: { "Content-Type": "application/json" },
            body:    JSON.stringify({ question, golden }),
            signal:  ABORTER.signal,
        });
    } catch (e) {
        _setStatus(`connection failed: ${e.message}`, "error");
        _resetButtons();
        return;
    }
    if (!resp.ok) {
        _setStatus(`HTTP ${resp.status}: ${resp.statusText}`, "error");
        _resetButtons();
        return;
    }
    _setStatus("streaming…", "streaming");

    const reader = resp.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    let currentEvent = "";
    let tokenCount = 0;
    let probeError = null;
    let completeData = null;

    function _processFrame(line) {
        if (!line) { currentEvent = ""; return; }
        if (line.startsWith("event:")) { currentEvent = line.slice(6).trim(); return; }
        if (!line.startsWith("data:"))  return;
        const ps = line.slice(5).trim();
        if (ps === "[DONE]") return;
        let ev;
        try { ev = JSON.parse(ps); } catch { return; }

        // `event: sources` arrives without a top-level `type` field.
        if (currentEvent === "sources" || ("sources" in ev && !("type" in ev))) {
            currentEvent = "";
            return;       // we surface sources via context_debug.chunks instead
        }
        const kind = ev.type;
        if (kind === "token") {
            tokenCount++;
            appendToken(ev.text || "", CURRENT_FX);
        } else if (kind === "context_debug") {
            renderRouting(ev.statute_routing || {});
            renderAnchor(ev.anchor || {}, requiredSet);
            renderChunks(ev.chunks || [], ev.guidance || {});
        } else if (kind === "timing") {
            renderTiming(ev);
        } else if (kind === "probe_complete") {
            completeData = ev;
        } else if (kind === "probe_error") {
            probeError = ev.error || "unknown probe error";
        }
    }

    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });
            let nl;
            while ((nl = buffer.indexOf("\n")) >= 0) {
                const line = buffer.slice(0, nl).trim();
                buffer = buffer.slice(nl + 1);
                _processFrame(line);
            }
        }
    } catch (e) {
        if (e.name === "AbortError") {
            _setStatus("cancelled by user", "error");
        } else {
            _setStatus(`stream error: ${e.message}`, "error");
        }
        _resetButtons();
        return;
    }

    finalizeAnswerStage($("#live-answer")?.dataset?.raw?.length || 0);

    if (probeError) {
        _setStatus(`upstream error: ${probeError}`, "error");
        _setStage("anchor", "error");
        _setStage("answer", "error");
    } else if (completeData) {
        _setStatus(`complete — ${tokenCount} tokens streamed`, "complete");
        renderDelta(completeData.delta, completeData.actual);
    } else {
        _setStatus("stream ended without probe_complete event", "error");
    }
    _resetButtons();
}

function _resetButtons() {
    $("#probe-btn").disabled = false;
    $("#cancel-btn").classList.add("hidden");
    ABORTER = null;
}

/* ------------------------------------------------------------------ */
/* BOOT                                                                */
/* ------------------------------------------------------------------ */

async function main() {
    await Promise.all([loadConfig(), loadReport()]);
    renderHeaderMeta();
    populateFixturePicker();

    // Auto-select from URL fragment if provided: probe.html#g001
    const urlId = (location.hash || "").replace(/^#/, "");
    if (urlId && REPORT) {
        const fx = REPORT.fixtures.find(f => f.id === urlId);
        if (fx) {
            $("#fixture-picker").value = urlId;
            CURRENT_FX = fx;
            $("#question-input").value = fx.question || "";
            renderGolden(fx);
        }
    }

    $("#fixture-picker").addEventListener("change", (ev) => {
        const id = ev.target.value;
        if (!id) {
            CURRENT_FX = null;
            $("#question-input").value = "";
            renderGolden(null);
            history.replaceState(null, "", location.pathname + location.search);
            return;
        }
        const fx = REPORT && REPORT.fixtures.find(f => f.id === id);
        if (!fx) return;
        CURRENT_FX = fx;
        $("#question-input").value = fx.question || "";
        renderGolden(fx);
        history.replaceState(null, "", `#${id}`);
    });

    // If user edits the question manually, detach from the fixture's golden —
    // a modified question can't be diffed against the original golden.
    $("#question-input").addEventListener("input", () => {
        if (!CURRENT_FX) return;
        if ($("#question-input").value.trim() !== (CURRENT_FX.question || "").trim()) {
            // Keep the golden visible but warn the user.
            const body = $("#golden-body");
            if (!body.querySelector(".edit-warning")) {
                const warn = document.createElement("div");
                warn.className = "edit-warning";
                warn.style.cssText = "background:var(--warn-bg);color:var(--warn-fg);padding:6px 10px;border-radius:4px;margin-bottom:10px;font-size:0.82rem";
                warn.textContent = "⚠ question edited — delta will still be computed against this fixture's golden, but the comparison may be off-topic.";
                body.insertBefore(warn, body.firstChild);
            }
        }
    });

    $("#probe-btn").disabled = false;
    $("#probe-btn").addEventListener("click", runProbe);
    $("#cancel-btn").addEventListener("click", () => {
        if (ABORTER) ABORTER.abort();
    });

    // Keyboard shortcut: Ctrl/Cmd + Enter to probe
    $("#question-input").addEventListener("keydown", (ev) => {
        if ((ev.ctrlKey || ev.metaKey) && ev.key === "Enter") {
            ev.preventDefault();
            runProbe();
        }
    });
}

main();
