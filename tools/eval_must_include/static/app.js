/* eval delta viewer — vanilla JS, no deps.
 *
 * Loads report.json (produced by run_eval.py), renders one expandable row per
 * fixture with side-by-side "what golden expects" vs "what the LLM actually
 * received" panes for each measurable dimension:
 *   - section_recall        (required_sections vs anchor.sections)
 *   - section_faithfulness  (citations.legislation.key_rule vs anchor preview text)
 *   - must_include          (key_points vs context blob + answer)
 *   - no_violation          (forbidden_claims vs context + answer)
 *   - accuracy              (golden answer vs model answer, token agreement)
 *
 * The model name shown throughout the UI is whatever the server's
 * /v1/models endpoint reported at eval time (stored in report.meta.llm_name),
 * so the tool isn't pinned to any particular vendor or version.
 *
 * All matching is pure text/word-overlap — no LLM-judge involved.
 */

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

// Set by main() from report.meta.llm_name so every renderer uses the same
// label — keeps the tool model-agnostic.
let LLM_NAME = "the LLM";

const escapeHtml = (s) => (s ?? "").toString()
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");

const pct = (num, den) => den > 0 ? Math.round((num / den) * 100) : 0;

function bar(value, klass = "") {
    const w = Math.min(100, Math.max(0, value * 100));
    return `<span class="bar ${klass}"><span style="width:${w}%"></span></span>`;
}

function badge(text, klass) {
    return `<span class="badge ${klass}">${escapeHtml(text)}</span>`;
}

function verdictBadge(v) {
    const map = {
        ok: "ok", answer_gap: "warn", context_gap: "warn", bad: "bad",
    };
    return badge(v, map[v] || "muted");
}

/* ------------------------------------------------------------------ */
/* SUMMARY                                                             */
/* ------------------------------------------------------------------ */
function renderSummary(report) {
    const m = report.meta || {};
    const srcInfo = m.source
        ? `source=${escapeHtml(m.source)}` +
          (m.by_source ? ` (${Object.entries(m.by_source).map(([k,v])=>`${k}=${v}`).join(", ")})` : "")
        : "";
    const llmInfo = m.llm_name
        ? `LLM=<code>${escapeHtml(m.llm_name)}</code>`
        : "";
    $("#meta").innerHTML =
        `<div>${escapeHtml(m.ts || "")}</div>` +
        `<div>${escapeHtml(m.url || "")}` +
        (m.git_commit ? ` &middot; <code>${escapeHtml(m.git_commit)}</code>` : "") +
        (llmInfo ? ` &middot; ${llmInfo}` : "") +
        (srcInfo ? ` &middot; ${srcInfo}` : "") +
        `</div>`;
    if (m.thresholds) {
        $("#thresholds").innerHTML =
            `supportable&nbsp;≥&nbsp;${m.thresholds.supportable} &middot; covered&nbsp;≥&nbsp;${m.thresholds.covered}`;
    }

    const s = report.summary || {};
    const sr = s.section_recall || {};
    const mi = s.must_include || {};
    const nv = s.no_violation || {};
    const sf = s.section_faithfulness || {};
    const total = s.fixtures || 0;

    const cards = [
        {
            title: "fixtures by verdict",
            metrics: [
                ["ok",         s.ok ?? 0,          "ok"],
                ["answer_gap", s.answer_gap ?? 0,  "answer_gap"],
                ["context_gap", s.context_gap ?? 0, "context_gap"],
                ["bad",        s.bad ?? 0,         "bad"],
                ["errors",     s.errors ?? 0,      "errors"],
                ["total",      total,              ""],
            ],
        },
        {
            title: "section_recall",
            metrics: [
                ["required",   sr.required_total ?? 0],
                ["retrieved",  sr.retrieved_total ?? 0],
                ["missing",    sr.missing ?? 0, (sr.missing ?? 0) ? "bad" : "ok"],
                ["cited in answer", sr.cited_in_answer ?? 0],
                ["cited not retrieved (hallucinated)",
                    sr.cited_but_not_retrieved ?? 0,
                    (sr.cited_but_not_retrieved ?? 0) ? "bad" : "ok"],
            ],
        },
        {
            title: "must_include  (key_points)",
            metrics: [
                ["total",         mi.total ?? 0],
                ["supportable",   mi.supportable ?? 0],
                ["covered",       mi.covered ?? 0],
                ["unsupportable", mi.unsupportable ?? 0,
                    (mi.unsupportable ?? 0) ? "bad" : "ok"],
                ["coverage",      `${pct(mi.covered ?? 0, mi.total ?? 0)}%`],
            ],
        },
        {
            title: "section_faithfulness  (key_rule substrate)",
            metrics: [
                ["citations",  sf.total ?? 0],
                ["ok",         sf.ok ?? 0,        "ok"],
                ["truncated",  sf.truncated ?? 0, (sf.truncated ?? 0) ? "bad" : "ok"],
                ["missing",    sf.missing ?? 0,   (sf.missing ?? 0)   ? "bad" : "ok"],
            ],
        },
        {
            title: "no_violation  (forbidden claims)",
            metrics: [
                ["in answer",  nv.in_answer ?? 0,
                    (nv.in_answer ?? 0) ? "bad" : "ok"],
                ["in context", nv.in_context ?? 0,
                    (nv.in_context ?? 0) ? "answer_gap" : "ok"],
            ],
        },
    ];

    $("#summary").innerHTML = cards.map(c => `
        <div class="card">
            <h3>${escapeHtml(c.title)}</h3>
            <div class="metrics">
                ${c.metrics.map(([k, v, klass]) =>
                    `<div class="metric"><span class="v ${klass || ""}">${escapeHtml(String(v))}</span>
                                         <span class="k">${escapeHtml(k)}</span></div>`).join("")}
            </div>
        </div>
    `).join("");
}

/* ------------------------------------------------------------------ */
/* PER-FIXTURE                                                         */
/* ------------------------------------------------------------------ */
function dimSectionRecall(d) {
    const sr = d.section_recall || {};
    const required  = sr.required || [];
    const retrieved = sr.retrieved || [];
    const missing   = new Set(sr.sections_missing || []);
    const present   = new Set(sr.sections_present || []);
    const cited     = new Set(sr.required_cited_in_answer || []);
    const halluc    = sr.sections_cited_but_not_retrieved || [];

    const lhs = required.length === 0
        ? `<li class="warn">(none required)</li>`
        : required.map(s => {
            const inAnchor = present.has(s) || missing.size === 0 && required.length === retrieved.length;
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
            return `<li class="${klass}">
                <code>${escapeHtml(s)}</code>
                <small style="color:var(--muted)"> · ${escapeHtml(extra)}</small>
            </li>`;
        }).join("");

    const halluc_html = halluc.length === 0 ? "" :
        `<div style="margin-top:8px;color:var(--err-fg);font-size:0.82rem">
            <strong>⚠ cited but not retrieved (likely hallucinated):</strong>
            ${halluc.map(s => `<code>${escapeHtml(s)}</code>`).join(", ")}
         </div>`;

    return `
        <div class="dim-body">
            <div class="diff">
                <div class="pane">
                    <h4>Golden — required_sections</h4>
                    <ul>${lhs}</ul>
                </div>
                <div class="pane">
                    <h4>Anchor — retrieved (${escapeHtml(LLM_NAME)} saw these)</h4>
                    <ul>${rhs}</ul>
                </div>
            </div>
            ${halluc_html}
        </div>`;
}

function dimSectionFaithfulness(d) {
    const sf = d.section_faithfulness || {};
    const cites = sf.citations || [];
    if (!cites.length) {
        return `<div class="dim-body"><div class="empty">no citations declared in golden</div></div>`;
    }
    const rows = cites.map(c => {
        const klass =
            c.verdict === "ok" ? "ok" :
            c.verdict === "section_missing" ? "bad" : "warn";
        const verdictLabel = {
            "ok": "ok — key_rule substrate present in anchor preview",
            "section_present_substrate_truncated":
                "section retrieved BUT key_rule text not in 600-char preview (truncation)",
            "section_missing": "section not retrieved at all",
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
    return `
        <div class="dim-body">
            <table class="kp-table">
                <thead><tr>
                    <th>section</th><th>title</th><th>key_rule (golden expects this)</th>
                    <th>overlap with anchor preview</th><th>verdict</th>
                </tr></thead>
                <tbody>${rows}</tbody>
            </table>
        </div>`;
}

function dimMustInclude(d) {
    const mi = d.must_include || {};
    const points = mi.key_points || [];
    if (!points.length) {
        return `<div class="dim-body"><div class="empty">no key_points in this fixture</div></div>`;
    }
    const rows = points.map(p => {
        const klass =
            p.verdict === "ok" ? "ok" :
            p.verdict === "unsupportable" ? "bad" :
            p.verdict === "covered_without_evidence" ? "info" : "warn";
        const verdictLabel = {
            "ok":                        "covered + supportable",
            "missed_in_answer":          "supportable from context but missing from answer",
            "covered_without_evidence":  "in answer but NO evidence in context (model prior knowledge / paraphrase)",
            "unsupportable":             "context cannot support this — answer cannot cover without hallucinating",
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
    return `
        <div class="dim-body">
            <table class="kp-table">
                <thead><tr>
                    <th>key_point (golden expects this in ${escapeHtml(LLM_NAME)}'s answer)</th>
                    <th>overlap with retrieved context</th>
                    <th>overlap with model answer</th>
                    <th>verdict</th>
                </tr></thead>
                <tbody>${rows}</tbody>
            </table>
        </div>`;
}

function dimNoViolation(d) {
    const nv = d.no_violation || {};
    const items = nv.forbidden || [];
    if (!items.length) {
        return `<div class="dim-body"><div class="empty">no forbidden_claims declared</div></div>`;
    }
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
    return `
        <div class="dim-body">
            <table class="kp-table">
                <thead><tr>
                    <th>forbidden phrase</th><th>in retrieved context</th>
                    <th>in model answer</th><th>verdict</th>
                </tr></thead>
                <tbody>${rows}</tbody>
            </table>
        </div>`;
}

function dimAssets(rec) {
    const a = rec.actual || {};
    const anchor = (a.anchor || {}).sections || [];
    const chunks = a.chunks || [];
    const g = a.guidance || {};
    const required = new Set((rec.delta?.section_recall?.required) || []);

    const anchorCards = anchor.length === 0
        ? `<div class="empty">no anchor sections retrieved</div>`
        : anchor.map(s => {
            const cls = required.has(s.section_id) ? "required" : "extra";
            return `<div class="asset ${cls}" onclick="this.classList.toggle('expanded')">
                <div class="id">${escapeHtml(s.section_id || s.document_id)}
                    <span class="score">${s.tokens || 0} tok</span></div>
                <div class="title">${escapeHtml(s.title || "")}</div>
                <div class="preview">${escapeHtml(s.preview || "(no preview)")}</div>
            </div>`;
        }).join("");

    const chunkCards = chunks.length === 0
        ? `<div class="empty">no case chunks retrieved</div>`
        : chunks.map(c => `<div class="asset" onclick="this.classList.toggle('expanded')">
            <div class="id">${escapeHtml(c.document_id || "")}
                <span class="score">${(c.score || 0).toFixed(3)}</span>
                <span class="score">${c.tokens || 0} tok</span></div>
            <div class="title">${escapeHtml(c.date || "")}</div>
            <div class="preview">${escapeHtml(c.preview || c.full_text || "")}</div>
        </div>`).join("");

    const guidance_html = !g.injected
        ? `<div class="empty">no guidance injected</div>`
        : `<div class="asset">
            <div class="id">${escapeHtml(g.source || "guidance")}
                <span class="score">${g.score?.toFixed?.(3) ?? ""}</span></div>
            <div class="title">${escapeHtml(g.court_name || "")} (${escapeHtml(g.reason || "")})</div>
           </div>`;

    const budget = a.budget || {};
    const timing = a.timing || {};

    return `<div class="dim-body">
        <div style="margin-bottom:10px;font-size:0.84rem;color:var(--muted)">
            <strong>budget:</strong>
            ${budget.total_tokens ?? "?"} tokens total
            (${budget.anchor_tokens ?? 0} anchor + ${budget.chunk_tokens ?? 0} chunks),
            ${budget.sources_sent ?? 0} sources sent,
            ctx_limit ${budget.ctx_limit ?? "?"}
            &nbsp;·&nbsp;
            <strong>timing:</strong>
            embed ${(timing.embed_ms ?? 0).toFixed?.(0) ?? timing.embed_ms}ms ·
            anchor ${(timing.anchor_ms ?? 0).toFixed?.(0) ?? timing.anchor_ms}ms ·
            ttft ${(timing.ttft_ms ?? 0).toFixed?.(0) ?? timing.ttft_ms}ms ·
            total ${(timing.total_ms ?? 0).toFixed?.(0) ?? timing.total_ms}ms
        </div>
        <h4 style="font-size:0.74rem;color:var(--muted);margin-bottom:4px">anchor (legislation ${escapeHtml(LLM_NAME)} received)</h4>
        <div class="cards">${anchorCards}</div>
        <h4 style="font-size:0.74rem;color:var(--muted);margin:10px 0 4px">guidance</h4>
        <div class="cards">${guidance_html}</div>
        <h4 style="font-size:0.74rem;color:var(--muted);margin:10px 0 4px">case chunks (${chunks.length})</h4>
        <div class="cards">${chunkCards}</div>
    </div>`;
}

function renderMarkdown(text) {
    if (!text) return "";
    // Escape HTML first, then apply markdown transforms on safe text.
    let s = escapeHtml(text);
    // Bold and italic (process ** before *)
    s = s.replace(/\*\*(.+?)\*\*/g, "<strong>$1</strong>");
    s = s.replace(/(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)/gs, "<em>$1</em>");
    // Inline code
    s = s.replace(/`([^`]+)`/g, "<code>$1</code>");

    // Line-by-line: lists and paragraphs
    const lines = s.split("\n");
    const out = [];
    let inUl = false, inOl = false;
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const ulM = line.match(/^[-*]\s+(.*)/);
        const olM = line.match(/^(\d+)\.\s+(.*)/);
        if (ulM) {
            if (inOl) { out.push("</ol>"); inOl = false; }
            if (!inUl) { out.push("<ul>"); inUl = true; }
            out.push(`<li>${ulM[1]}</li>`);
        } else if (olM) {
            if (inUl) { out.push("</ul>"); inUl = false; }
            if (!inOl) { out.push("<ol>"); inOl = true; }
            out.push(`<li>${olM[2]}</li>`);
        } else {
            if (inUl) { out.push("</ul>"); inUl = false; }
            if (inOl) { out.push("</ol>"); inOl = false; }
            // Blank line -> paragraph break; non-empty line -> preserve with br
            out.push(line === "" ? "<br>" : line + "<br>");
        }
    }
    if (inUl) out.push("</ul>");
    if (inOl) out.push("</ol>");
    return out.join("\n");
}

function highlightAnswer(answer, rec) {
    let out = renderMarkdown(answer);
    const required = rec.golden?.required_sections || [];
    for (const s of required) {
        const m = s.match(/^s(\d+)(.*)$/);
        if (!m) continue;
        const re = new RegExp(`\\b(s\\s*${m[1]}${m[2].replace(/[()]/g, "\\$&")}|[Ss]ection\\s+${m[1]}${m[2].replace(/[()]/g, "\\$&")})\\b`, "g");
        out = out.replace(re, x => `<mark class="req">${x}</mark>`);
    }
    for (const c of (rec.golden?.forbidden_claims || [])) {
        const re = new RegExp(c.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi");
        out = out.replace(re, x => `<mark class="forb">${x}</mark>`);
    }
    return out;
}

function dimAnswers(rec) {
    const golden = rec.golden?.answer || "";
    const actual = rec.actual?.answer || "";
    const jacc = rec.delta?.accuracy?.answer_token_jaccard ?? 0;
    return `<div class="dim-body">
        <div style="font-size:0.82rem;color:var(--muted);margin-bottom:6px">
            token jaccard golden↔actual = <strong>${jacc}</strong>
            (1.0 means identical token set; 0 means disjoint).
            <span style="margin-left:8px">
                <mark class="req">required section</mark>
                <mark class="forb">forbidden phrase</mark>
            </span>
        </div>
        <div class="answers">
            <div class="pane">
                <h4>Golden answer (reference)</h4>
                <div class="text">${highlightAnswer(golden, rec)}</div>
            </div>
            <div class="pane">
                <h4>Model answer (what ${escapeHtml(LLM_NAME)} produced)</h4>
                <div class="text">${highlightAnswer(actual, rec)}</div>
            </div>
        </div>
    </div>`;
}

function fixtureRow(rec) {
    if (rec.error) {
        return `<div class="fixture">
            <div class="fixture-head">
                <span class="fixture-id">${escapeHtml(rec.id)}</span>
                <span class="fixture-q">${escapeHtml(rec.topic || "")}</span>
                ${badge("error", "bad")}
            </div>
            <div class="error-row">${escapeHtml(rec.error)}</div>
        </div>`;
    }

    const d = rec.delta || {};
    const v = d.verdict || "?";
    const mi = d.must_include?.summary || {};
    const sf = d.section_faithfulness?.summary || {};
    const sr = d.section_recall || {};

    const badges = [
        verdictBadge(v),
        badge(`sec ${sr.sections_present?.length ?? 0}/${sr.required?.length ?? 0}`,
              (sr.sections_missing?.length ?? 0) ? "bad" : "ok"),
        badge(`kp ${mi.covered ?? 0}/${mi.total ?? 0}`,
              (mi.unsupportable ?? 0) ? "bad" :
              (mi.covered ?? 0) < (mi.total ?? 0) ? "warn" : "ok"),
        badge(`cite ${sf.ok ?? 0}/${sf.total ?? 0}`,
              (sf.truncated ?? 0) || (sf.missing ?? 0) ? "warn" : "ok"),
    ];
    if ((d.no_violation?.forbidden_in_answer?.length ?? 0) > 0) {
        badges.push(badge("violation", "bad"));
    }
    if ((sr.sections_cited_but_not_retrieved?.length ?? 0) > 0) {
        badges.push(badge("halluc cite", "bad"));
    }

    return `<div class="fixture" data-verdict="${v}" data-fixture-id="${escapeHtml(rec.id)}" data-search="${escapeHtml((rec.id + " " + rec.topic + " " + (rec.question || "")).toLowerCase())}">
        <div class="fixture-head" onclick="this.parentElement.classList.toggle('open')">
            <div>
                <div class="fixture-id">${escapeHtml(rec.id)}</div>
                <div class="fixture-topic">${escapeHtml(rec.topic || "")}${rec.source ? ` <span class="src">${escapeHtml(rec.source)}</span>` : ""}</div>
            </div>
            <div class="fixture-q">${escapeHtml(rec.question || "")}</div>
            <div class="fixture-badges">${badges.join("")}</div>
        </div>
        <div class="fixture-body">
            ${rec.key_point_origin ? `<div class="origin">key_points origin: <code>${escapeHtml(rec.key_point_origin)}</code></div>` : ""}
            <div style="text-align:right;margin-top:8px">
                <button class="copy-fixture" data-fixture-id="${escapeHtml(rec.id)}"
                        title="Copy this fixture's delta as markdown (LLM-friendly)">📋 copy this fixture as markdown</button>
            </div>
            <div class="dim">
                <h2>section_recall <span class="tag">required_sections vs anchor.sections</span></h2>
                ${dimSectionRecall(d)}
            </div>
            <div class="dim">
                <h2>section_faithfulness <span class="tag">citations.legislation.key_rule vs anchor preview text</span></h2>
                ${dimSectionFaithfulness(d)}
            </div>
            <div class="dim">
                <h2>must_include <span class="tag">key_points vs context blob &amp; answer (word-overlap)</span></h2>
                ${dimMustInclude(d)}
            </div>
            <div class="dim">
                <h2>no_violation <span class="tag">forbidden_claims vs context &amp; answer (substring)</span></h2>
                ${dimNoViolation(d)}
            </div>
            <div class="dim">
                <h2>retrieval assets <span class="tag">what the LLM actually saw</span></h2>
                ${dimAssets(rec)}
            </div>
            <div class="dim">
                <h2>answer comparison <span class="tag">golden vs model, with highlights</span></h2>
                ${dimAnswers(rec)}
            </div>
        </div>
    </div>`;
}

/* ------------------------------------------------------------------ */
/* MARKDOWN EXPORT (in-browser; mirrors analyze.py)                    */
/* ------------------------------------------------------------------ */
function _trunc(s, n = 160) {
    s = (s || "").replace(/\s+/g, " ").trim();
    return s.length <= n ? s : s.slice(0, n - 1) + "…";
}

function _fixtureMd(rec) {
    if (rec.error) {
        return `## ${rec.id} ${rec.topic || ""} [ERROR]\n\n\`${rec.error}\`\n`;
    }
    const d = rec.delta || {};
    const v = d.verdict || "?";
    const sr = d.section_recall || {};
    const sf = d.section_faithfulness || {};
    const mi = d.must_include || {};
    const nv = d.no_violation || {};
    const acc = d.accuracy || {};
    const out = [];
    out.push(`## ${rec.id} ${rec.topic || ""} [${v}]`);
    out.push("");
    if (rec.source || rec.key_point_origin) {
        const bits = [];
        if (rec.source) bits.push(`source: ${rec.source}`);
        if (rec.key_point_origin) bits.push(`key_points: ${rec.key_point_origin}`);
        out.push(`_${bits.join(" · ")}_`);
        out.push("");
    }
    out.push(`**Q:** ${_trunc(rec.question || "", 220)}`);
    out.push("");

    // section_recall
    const req = sr.required || [];
    if (req.length) {
        const flags = [];
        const missing = sr.sections_missing || [];
        const cited = new Set(sr.required_cited_in_answer || []);
        const halluc = sr.sections_cited_but_not_retrieved || [];
        if (missing.length) flags.push(`MISSING from anchor: ${missing.join(", ")}`);
        const notCited = req.filter(s => !cited.has(s) && !missing.includes(s));
        if (notCited.length) flags.push(`retrieved but NOT cited in answer: ${notCited.join(", ")}`);
        if (halluc.length) flags.push(`HALLUCINATED in answer (cited but not retrieved): ${halluc.join(", ")}`);
        if (!flags.length) flags.push("ok");
        out.push(`- **section_recall**: required=[${req.join(", ")}] → ${flags.join("; ")}`);
    } else {
        out.push("- **section_recall**: (no required_sections declared)");
    }

    // section_faithfulness
    const cites = sf.citations || [];
    if (cites.length) {
        const ss = sf.summary || {};
        out.push(`- **section_faithfulness**: ${ss.ok || 0}/${ss.total || 0} ok  ` +
                 `(${ss.truncated || 0} truncated, ${ss.missing || 0} missing)`);
        for (const c of cites) {
            if (c.verdict === "ok") continue;
            const tag = c.verdict === "section_present_substrate_truncated"
                ? "TRUNCATED"
                : c.verdict === "section_missing" ? "MISSING" : c.verdict;
            out.push(`    - ${tag} ${c.section}: "${_trunc(c.key_rule, 110)}" (overlap=${c.rule_overlap})`);
        }
    } else {
        out.push("- **section_faithfulness**: (no citations declared)");
    }

    // must_include
    const pts = mi.key_points || [];
    if (pts.length) {
        const s = mi.summary || {};
        let head = `- **must_include**: ${s.covered || 0}/${s.total || 0} covered, ${s.supportable || 0}/${s.total || 0} supportable`;
        if (s.unsupportable) head += `, **${s.unsupportable} unsupportable**`;
        out.push(head);
        for (const p of pts) {
            if (p.verdict === "ok") continue;
            const tag = {
                "missed_in_answer":         "MISSED IN ANSWER",
                "covered_without_evidence": "COVERED WITHOUT EVIDENCE",
                "unsupportable":            "UNSUPPORTABLE",
            }[p.verdict] || p.verdict;
            out.push(`    - ${tag}: "${_trunc(p.text, 140)}" (ctx=${p.context_overlap}, ans=${p.answer_overlap})`);
        }
    } else {
        out.push("- **must_include**: (no key_points declared)");
    }

    // no_violation
    const forb = nv.forbidden || [];
    if (forb.length) {
        const inAns = nv.forbidden_in_answer || [];
        const inCtx = nv.forbidden_in_context || [];
        if (!inAns.length && !inCtx.length) {
            out.push(`- **no_violation**: 0/${forb.length} hits — clean`);
        } else {
            out.push(`- **no_violation**: ${inAns.length} in answer, ${inCtx.length} in context`);
            for (const c of inAns) out.push(`    - VIOLATION (in answer): "${c}"`);
            for (const c of inCtx) {
                if (inAns.includes(c)) continue;
                out.push(`    - POISON RISK (in context): "${c}"`);
            }
        }
    } else {
        out.push("- **no_violation**: (no forbidden_claims declared)");
    }

    out.push(`- **accuracy**: answer↔golden token jaccard = ${acc.answer_token_jaccard ?? 0}`);
    out.push(`- **context_blob_chars**: ${d.context_blob_chars || 0}`);
    out.push("");
    return out.join("\n");
}

function _reportMd(report) {
    const m = report.meta || {};
    const s = report.summary || {};
    const n = s.fixtures || 1;
    const pct = (k) => `${s[k] || 0}/${n} (${Math.round(100 * (s[k] || 0) / n)}%)`;
    const out = [];
    out.push("# astraea.cpp eval delta — markdown analysis");
    out.push("");
    out.push(`- generated: \`${m.ts || ""}\``);
    out.push(`- url: \`${m.url || ""}\``);
    out.push(`- commit: \`${m.git_commit || ""}\``);
    if (m.llm_name) {
        out.push(`- LLM under test: \`${m.llm_name}\`` +
                 (m.llm_origin ? `  _(detected via ${m.llm_origin})_` : ""));
    }
    if (m.source) {
        out.push(`- fixture source: \`${m.source}\`` +
                 (m.by_source ? ` (${Object.entries(m.by_source).map(([k,v])=>`${k}=${v}`).join(", ")})` : ""));
    }
    if (m.thresholds) {
        out.push(`- thresholds: supportable ≥ ${m.thresholds.supportable}, covered ≥ ${m.thresholds.covered}`);
    }
    out.push("");
    out.push("## Verdict mix");
    out.push("");
    out.push(`- **ok**: ${pct("ok")}`);
    out.push(`- **answer_gap**: ${pct("answer_gap")}  _(context could have supported every key_point; model didn't surface them all)_`);
    out.push(`- **context_gap**: ${pct("context_gap")}  _(anchor truncation / unsupportable key_point / poison phrase in context)_`);
    out.push(`- **bad**: ${pct("bad")}  _(missing required section, forbidden phrase in answer, or hallucinated citation)_`);
    if (s.errors) out.push(`- **errors**: ${s.errors}`);
    out.push("");
    out.push("## Per-fixture deltas");
    out.push("");
    for (const r of (report.fixtures || [])) out.push(_fixtureMd(r));
    out.push("---");
    out.push("");
    out.push("_Generated by the eval delta viewer (client-side renderer; same shape as `tools/eval_must_include/analyze.py`)._");
    return out.join("\n");
}

async function _copy(text) {
    try {
        await navigator.clipboard.writeText(text);
        return true;
    } catch {
        // Fallback: hidden textarea
        const ta = document.createElement("textarea");
        ta.value = text;
        ta.style.position = "fixed";
        ta.style.opacity = "0";
        document.body.appendChild(ta);
        ta.select();
        let ok = false;
        try { ok = document.execCommand("copy"); } catch {}
        document.body.removeChild(ta);
        return ok;
    }
}

function _toast(msg) {
    const t = document.createElement("div");
    t.className = "toast";
    t.textContent = msg;
    document.body.appendChild(t);
    setTimeout(() => t.classList.add("show"), 10);
    setTimeout(() => { t.classList.remove("show"); setTimeout(() => t.remove(), 300); }, 2200);
}

/* ------------------------------------------------------------------ */
/* FILTERS                                                             */
/* ------------------------------------------------------------------ */
function applyFilters() {
    const wantVerdict = new Set(
        ["ok", "answer_gap", "context_gap", "bad"]
            .filter(k => $(`#f-${k}`).checked)
    );
    const wantErrors = $("#f-errors").checked;
    const q = $("#search").value.trim().toLowerCase();

    $$(".fixture").forEach(el => {
        const v = el.dataset.verdict || "";
        let show = (v && wantVerdict.has(v)) || (!v && wantErrors);
        if (show && q) show = (el.dataset.search || "").includes(q);
        el.style.display = show ? "" : "none";
    });
}

/* ------------------------------------------------------------------ */
/* BOOT                                                                */
/* ------------------------------------------------------------------ */
async function main() {
    let report;
    try {
        const resp = await fetch("report.json", { cache: "no-store" });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        report = await resp.json();
    } catch (e) {
        document.body.innerHTML =
            `<div class="error-row" style="margin:40px">
                Could not load <code>report.json</code>: ${escapeHtml(e.message)}<br><br>
                Generate one with:<br>
                <code>python tools/eval_must_include/run_eval.py</code>
            </div>`;
        return;
    }

    // Set the global model-name string before any renderer runs so every
    // dimension pane shows the same label (qwen3 / llama-3.1 / whatever).
    LLM_NAME = (report.meta && report.meta.llm_name) || "the LLM";

    renderSummary(report);

    const fixtures = report.fixtures || [];
    $("#fixtures").innerHTML = fixtures.length === 0
        ? `<div class="empty">no fixtures in report</div>`
        : fixtures.map(fixtureRow).join("");

    $$(".filters input").forEach(el => el.addEventListener("input", applyFilters));
    $("#expand-all").addEventListener("click",
        () => $$(".fixture").forEach(f => f.classList.add("open")));
    $("#collapse-all").addEventListener("click",
        () => $$(".fixture").forEach(f => f.classList.remove("open")));

    /* --- Permalinks ---------------------------------------------------
     * On load: if the URL hash matches a fixture id (e.g.
     *   https://tenancy-eval.localrun.ai/#g001
     * or
     *   https://tenancy-eval.localrun.ai/#2829435277226750),
     * expand and scroll to that fixture.
     *
     * On click: update window.location.hash to the open fixture so the
     * current URL is always a working permalink to the focused finding.
     *
     * No history pollution — replaceState keeps Back navigation sane.
     */
    function _focusFromHash() {
        const id = (location.hash || "").replace(/^#/, "");
        if (!id) return;
        const el = document.querySelector(`.fixture[data-fixture-id="${CSS.escape(id)}"]`);
        if (!el) return;
        el.classList.add("open");
        el.scrollIntoView({ behavior: "smooth", block: "start" });
    }
    _focusFromHash();
    window.addEventListener("hashchange", _focusFromHash);
    document.addEventListener("click", (ev) => {
        const head = ev.target.closest(".fixture-head");
        if (!head) return;
        const fx = head.parentElement;
        const id = fx?.dataset?.fixtureId;
        if (!id) return;
        // Only set the hash when the fixture is now open — closing should clear it.
        const newHash = fx.classList.contains("open") ? `#${id}` : "";
        if (newHash !== location.hash) {
            history.replaceState(null, "", newHash || location.pathname + location.search);
        }
    });

    $("#copy-link").addEventListener("click", async () => {
        const url = location.origin + location.pathname + location.search + location.hash;
        const ok = await _copy(url);
        _toast(ok ? `Copied ${url}` : "Copy failed");
    });

    $("#copy-llm").addEventListener("click", async () => {
        const md = _reportMd(report);
        const ok = await _copy(md);
        _toast(ok ? `Copied ${md.length} chars of markdown to clipboard`
                  : "Copy failed — open report.md directly");
    });
    $("#download-md").addEventListener("click", () => {
        // Prefer the server-rendered report.md so analyze.py and the viewer
        // stay byte-identical on the file system, but fall back to the
        // in-browser renderer when the file isn't there.
        fetch("report.md", { cache: "no-store" })
            .then(r => r.ok ? r.text() : Promise.reject(new Error(`HTTP ${r.status}`)))
            .catch(() => _reportMd(report))
            .then(md => {
                const blob = new Blob([md], { type: "text/markdown" });
                const url = URL.createObjectURL(blob);
                const a = document.createElement("a");
                a.href = url;
                a.download = "report.md";
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
            });
    });

    // Per-fixture copy buttons (delegated so they survive filter re-renders).
    document.addEventListener("click", async (ev) => {
        const btn = ev.target.closest(".copy-fixture");
        if (!btn) return;
        const id = btn.dataset.fixtureId;
        const rec = (report.fixtures || []).find(f => f.id === id);
        if (!rec) return;
        const md = "# astraea.cpp eval delta — single fixture\n\n" + _fixtureMd(rec);
        const ok = await _copy(md);
        _toast(ok ? `Copied ${rec.id} delta (${md.length} chars)`
                  : "Copy failed — see browser console");
    });

}

main();

// Auto-refresh while a partial eval run is in progress.
// Polls every 8s, stops once meta.partial is absent (run complete).
(async function liveRefresh() {
    let knownCount = 0;
    let active = false;

    async function poll() {
        try {
            const resp = await fetch("report.json", { cache: "no-store" });
            if (!resp.ok) return;
            const report = await resp.json();
            const partial = report.meta && report.meta.partial;
            if (!partial) { active = false; return; } // run finished
            if (partial.done !== knownCount) {
                knownCount = partial.done;
                // Re-render without losing open/expanded fixture state.
                const openIds = new Set(
                    Array.from(document.querySelectorAll(".fixture.open"))
                         .map(el => el.dataset.fixtureId));
                LLM_NAME = (report.meta && report.meta.llm_name) || "the LLM";
                renderSummary(report);
                const fixtures = report.fixtures || [];
                $("#fixtures").innerHTML = fixtures.length === 0
                    ? `<div class="empty">no fixtures in report</div>`
                    : fixtures.map(fixtureRow).join("");
                openIds.forEach(id => {
                    const el = document.querySelector(`.fixture[data-fixture-id="${CSS.escape(id)}"]`);
                    if (el) el.classList.add("open");
                });
                // Show live progress in header meta.
                const metaEl = $("#meta");
                if (metaEl && partial) {
                    const pct = Math.round((partial.done / partial.total) * 100);
                    const bar = document.createElement("div");
                    bar.className = "live-progress";
                    bar.innerHTML = `<span class="live-dot"></span> live &mdash; ${partial.done}/${partial.total} (${pct}%)`;
                    metaEl.appendChild(bar);
                }
            }
            active = true;
        } catch (_) { /* network blip, try again next tick */ }
        if (active !== false) setTimeout(poll, 8000);
    }

    // Start polling after a short delay so the initial render settles.
    setTimeout(poll, 5000);
}());
