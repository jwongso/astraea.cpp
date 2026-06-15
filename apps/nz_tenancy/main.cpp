// nz_tenancy — Phase 6A skeleton.
//
// Three handlers, no business logic yet — Phase 6B will swap the echo /
// synthetic-stream paths for real RAGPipeline + retrieve_anchor +
// Generator::generate_stream wiring.
//
//   GET  /health      → "ok\n"
//   POST /ask         → sanitize_question() + RAG + JSON answer + sources
//   POST /ask/stream  → sanitize_question() + RAG + SSE token stream
//                       (one "event: sources" frame with {sources, guidance_source}
//                        upfront, then "data: {\"token\":\"...\"}" per token,
//                        then "data: [DONE]\n\n")
//
// Config comes from astraea::Config::from_env() — see include/astraea/config.hpp
// for the env-var names. The only one this handler reads in Phase 6A is PORT.
//
// Test from another terminal:
//
//   curl http://localhost:8080/health
//   curl -X POST http://localhost:8080/ask -H 'Content-Type: application/json'
//        -d '{"question":"what is the bond limit?"}'
//   curl -N -X POST http://localhost:8080/ask/stream -H 'Content-Type: application/json'
//        -d '{"question":"what is the bond limit?"}'

#include <drogon/drogon.h>
#include <drogon/HttpResponse.h>
#include <mimalloc.h>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "astraea/anchor.hpp"
#include "astraea/config.hpp"
#include "astraea/feedback.hpp"
#include "astraea/timing.hpp"
#include "astraea/coordinator.hpp"
#include "astraea/detail/when_all.hpp"
#include "astraea/generator.hpp"
#include "astraea/health.hpp"
#include "astraea/in_process_coordinator.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/redis_coordinator.hpp"
#include "astraea/request_id.hpp"
#include "astraea/route_table.hpp"
#include "astraea/session.hpp"
#include "astraea/retriever.hpp"
#include "astraea/sanitize.hpp"
#include "nz_tenancy/jurisdiction.hpp"

#include <openssl/crypto.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// JSON shapes — named namespace (NOT anonymous) for glaze reflection.
// Anonymous-namespace types have internal linkage and break glaze's
// glz::detail::external<T>. Pattern established in PR #9.
// ---------------------------------------------------------------------------

namespace astraea::detail::nz_tenancy_app {

struct AskRequest {
    std::string question;
};

struct TokenResp {
    std::string token;
};

struct TokenChunk {
    std::string type = "token";
    std::string text;
};

// Source rendering for the JSON response. label is derived from the source
// payload via jurisdiction.format_source_label() so the same court/date
// formatting rules apply across every consumer.
struct SourceJson {
    std::string id;
    float       score = 0.0f;
    std::string label;
};

struct AskResponse {
    std::string             answer;
    std::vector<SourceJson> sources;
    std::optional<SourceJson> guidance_source; // nullopt if no MANUAL guidance injected
};

// Payload for the SSE "event: sources" frame on /ask/stream. Same shape as
// AskResponse minus `answer` — clients can render citation chips before the
// first token arrives.
struct LegSourceJson {
    std::string case_id;
    std::string title;
    std::string url;
};

struct SourcesEvent {
    std::string type = "sources";
    std::vector<SourceJson>    sources;
    std::vector<LegSourceJson> legislation;
    std::optional<SourceJson>  guidance_source;
};

// Legislation-grounded verification event. Replaces the Playwright-based
// web_verify: we surface the legislation text we already retrieved from
// Qdrant so the user can see exactly which Act sections back the answer.
struct VerificationSection {
    std::string url;
    std::string reference;
    std::string excerpt;
};

struct VerificationEvent {
    std::string type = "verification";
    std::vector<VerificationSection> sections;
};

// /healthz response shapes. Lifted from astraea::HealthReport into local
// JSON structs so we control the wire shape (renames + omit fields without
// touching the library type, e.g. drop `error` when "ok").
struct HealthCheckJson {
    std::string                name;
    std::string                status;
    int                        latency_ms;
    std::string                url;
    std::optional<std::string> error;
};
struct CoordinatorInfoJson {
    std::string backend;
    int         max_concurrency;
};
struct HealthzResponse {
    std::string                          status;
    std::vector<HealthCheckJson>         checks;
    std::optional<CoordinatorInfoJson>   coordinator;
};

// /feedback request body.
struct FeedbackRequest {
    std::string question;
    int         rating   = 0;
    std::string comment;
};

// question_log.jsonl entry: one per real question (X-No-Log absent).
struct QuestionLogEntry {
    std::string ts;
    std::string request_id;
    std::string q;
};

// Compact source/legislation shapes for route_debug.jsonl.
struct LogSource {
    std::string id;
    float       score = 0.0f;
};
struct LogLegSource {
    std::string id;
    std::string title;
};

// route_debug.jsonl entry: routing + retrieval + answer per question.
struct RouteDebugEntry {
    std::string              ts;
    std::string              request_id;
    std::string              q;
    std::string              rewritten;
    bool                     triggered = false;
    std::vector<std::string> matched_intents;
    std::vector<LogSource>   sources;
    std::vector<LogLegSource> legislation;
    std::string              answer;
    // Prompt accounting (always emitted, even when ASTRAEA_ENABLE_TIMING is off).
    // context_chars is the char-count of the assembled user message sent to the
    // LLM; context_chunks is total cases + legislation + guidance chunks.
    // Cheap to compute and lets operators correlate prompt size with TTFT in
    // route_debug.jsonl without enabling the SSE timing event.
    int                      context_chars  = 0;
    int                      context_chunks = 0;
};

// feedback.jsonl entry.
struct FeedbackEntry {
    std::string ts;
    std::string request_id;
    std::string question;
    int         rating  = 0;
    std::string comment;
};

#ifdef ASTRAEA_ENABLE_TIMING
// SSE "timing" event emitted at the end of /ask/stream.
// Named slots mirror Python core/timing.py's top-level aggregates.
// `detail` carries all raw steps for client-side drill-down.
struct TimingEvent {
    std::string              type           = "timing";
    std::string              request_id;
    double                   sanitize_ms    = 0.0;
    double                   rewrite_ms     = 0.0;
    double                   embed_ms       = 0.0;
    double                   retrieve_ms    = 0.0;
    double                   anchor_ms      = 0.0;
    double                   guidance_ms    = 0.0;
    double                   context_ms     = 0.0;
    double                   llm_wait_ms    = 0.0;
    double                   ttft_ms        = 0.0;
    double                   generation_ms  = 0.0;
    double                   total_ms       = 0.0;
    // Prompt accounting (see BENCHMARK_PERF.md "context size hypothesis").
    // context_chars: total chars of the assembled user message sent to the LLM.
    // context_chunks: total chunks (cases + legislation + guidance) injected.
    int                      context_chars  = 0;
    int                      context_chunks = 0;
    std::vector<astraea::TimingStep> detail;
};
#endif // ASTRAEA_ENABLE_TIMING

} // namespace astraea::detail::nz_tenancy_app

namespace {

using namespace astraea::detail::nz_tenancy_app;

// ---------------------------------------------------------------------------
// Startup verification: confirm mimalloc is actually overriding the global
// allocator. The override comes from mimalloc_override.cpp in this same
// executable — if that TU is not linked, every `new` falls through to glibc
// and mi_is_in_heap_region() returns false.
//
// Skipped under ASan because ASan installs its own allocator interceptors
// that take precedence over mimalloc's; the probe would always fail and
// the dev preset would refuse to start. Production binaries (Release)
// are the only ones that actually need (and get) the override.
// ---------------------------------------------------------------------------

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ASTRAEA_ASAN_ACTIVE 1
#  endif
#endif

void verify_mimalloc_override() {
#ifdef ASTRAEA_ASAN_ACTIVE
    LOG_WARN << "ASan build: skipping mimalloc override verification "
                "(sanitiser interceptors take precedence over mimalloc)";
    return;
#else
    auto* probe = new int(42);
    const bool overridden = mi_is_in_heap_region(probe);
    delete probe;
    if (!overridden) {
        std::fprintf(stderr,
            "FATAL: mimalloc allocator override is not active. "
            "Check that apps/nz_tenancy/mimalloc_override.cpp is linked "
            "into this executable.\n");
        std::abort();
    }
    LOG_INFO << "mimalloc v" << mi_version() << " allocator override active";
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ISO-8601 UTC timestamp matching Python's datetime.now(timezone.utc).isoformat()
// format: "2026-06-13T10:00:00.123456+00:00".
std::string utc_iso8601() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto us  = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    char result[48];
    std::snprintf(result, sizeof(result), "%s.%06lld+00:00", buf,
                  static_cast<long long>(us.count()));
    return result;
}

bool is_no_log(const drogon::HttpRequestPtr& req) {
    return req->getHeader("x-no-log") == "1";
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

drogon::HttpResponsePtr text_response(drogon::HttpStatusCode code, std::string body) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setBody(std::move(body));
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    return resp;
}

drogon::HttpResponsePtr json_response(drogon::HttpStatusCode code, std::string body) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(code);
    resp->setBody(std::move(body));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    return resp;
}

// Per-IP concurrent request limiter. Prevents a single client from
// monopolising all LLM capacity under IP_MAX_CONCURRENCY > 0.
//
// Note: getPeerAddr().toIp() returns the direct peer address. Behind a
// Cloudflare Tunnel the peer is the tunnel edge IP, not the end-user's IP.
// For true per-client limiting in a tunnelled deployment, read the
// CF-Connecting-IP or X-Forwarded-For header instead, and set
// IP_MAX_CONCURRENCY=0 to disable this guard (use the proxy-level equivalent).
struct IpLimiter {
    explicit IpLimiter(int max) noexcept : _max(max) {}

    struct Permit {
        IpLimiter*  _lim = nullptr;
        std::string _ip;
        Permit() = default;
        Permit(IpLimiter* lim, std::string ip) : _lim(lim), _ip(std::move(ip)) {}
        Permit(Permit&& o) noexcept : _lim(o._lim), _ip(std::move(o._ip)) { o._lim = nullptr; }
        Permit& operator=(Permit&& o) noexcept {
            if (this != &o) { reset(); _lim = o._lim; _ip = std::move(o._ip); o._lim = nullptr; }
            return *this;
        }
        ~Permit() { reset(); }
        void reset() noexcept { if (_lim) { _lim->release(_ip); _lim = nullptr; } }
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
    };

    // Returns a Permit on success; nullopt if this IP is already at the limit.
    std::optional<Permit> try_acquire(const std::string& ip) {
        std::lock_guard<std::mutex> lock(_mu);
        auto& count = _counts[ip];
        if (count >= _max) return std::nullopt;
        ++count;
        return Permit{this, ip};
    }

    void release(const std::string& ip) noexcept {
        std::lock_guard<std::mutex> lock(_mu);
        auto it = _counts.find(ip);
        if (it != _counts.end() && --it->second == 0)
            _counts.erase(it);
    }

private:
    int                                  _max;
    std::mutex                           _mu;
    std::unordered_map<std::string, int> _counts;
};

// Parse + sanitise. On success populates `out` and returns nullptr; on any
// JSON-parse or sanitize failure returns a built error response (and `out`
// is left empty). Caller forwards the response directly to the Drogon
// callback when non-null.

drogon::HttpResponsePtr parse_and_sanitize(const drogon::HttpRequestPtr& req,
                                            std::string& out) {
    AskRequest parsed{};
    if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, req->getBody()); err) {
        return text_response(drogon::k400BadRequest, "Invalid JSON\n");
    }
    try {
        out = astraea::sanitize_question(parsed.question);
    } catch (const astraea::SanitizeError& e) {
        return text_response(
            static_cast<drogon::HttpStatusCode>(e.http_status),
            std::string{e.what()} + "\n");
    }
    return nullptr;  // success
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

drogon::HttpResponsePtr health_handler() {
    return text_response(drogon::k200OK, "ok\n");
}

// Deep readiness probe. Pings every upstream the binary needs (Qdrant + LLM
// chat + embed + optional rerank) and surfaces the coordinator backend info.
// Returns 200 when overall=="ok", 503 otherwise - shape matches k8s
// readinessProbe expectations.
drogon::Task<drogon::HttpResponsePtr> healthz_handler(
    astraea::HealthProber&            prober,
    const astraea::CoordinatorClient* coordinator_or_null)
{
    astraea::HealthReport rep = co_await prober.probe();

    HealthzResponse out;
    out.status = rep.overall;
    out.checks.reserve(rep.checks.size());
    for (const auto& c : rep.checks) {
        out.checks.push_back({
            c.name, c.status, c.latency_ms, c.url, c.error,
        });
    }
    if (coordinator_or_null) {
        out.coordinator = CoordinatorInfoJson{
            coordinator_or_null->backend_name(),
            coordinator_or_null->max_concurrency(),
        };
    }

    std::string body;
    if (auto e = glz::write_json(out, body); e) {
        co_return text_response(drogon::k500InternalServerError,
                                "healthz: serialization failed\n");
    }
    const auto code = (out.status == "ok")
        ? drogon::k200OK
        : drogon::k503ServiceUnavailable;
    co_return json_response(code, std::move(body));
}

// ---------------------------------------------------------------------------
// RAG assembly: shared by /ask and (in 6C.3) /ask/stream.
// retrieve -> retrieve_anchor -> retrieve_manual_guidance -> build messages.
// ---------------------------------------------------------------------------

struct AssembledRequest {
    std::vector<astraea::ChatMessage>   messages;
    std::vector<astraea::QdrantPoint>   sources;
    std::optional<astraea::QdrantPoint> guidance_source;
    // Logging fields - populated by assemble_request, written by the handler.
    std::string                         rewritten_q;     // empty when same as original
    std::vector<astraea::QdrantPoint>   leg_sources;     // from anchor
    std::vector<std::string>            matched_intents;
    bool                                route_triggered = false;
    // Prompt-size accounting. Populated at the end of assemble_request so
    // the timing event can correlate slow TTFT / generation against context
    // bloat. context_chars is the size of the assembled user message (incl.
    // the trailing Question:); context_chunks is the total chunk count fed
    // to the LLM (cases + legislation + guidance). See BENCHMARK_PERF.md.
    int                                 context_chars   = 0;
    int                                 context_chunks  = 0;
    // Timing instrumentation. No-op when ASTRAEA_ENABLE_TIMING is not defined.
    astraea::TimingCollector            timer;
};

std::string build_context_block(
    const astraea::RetrieveResult&            retrieved,
    const astraea::AnchorResult&              anchor,
    const astraea::GuidanceResult&            guidance)
{
    // RetrieveResult contract: texts[i] is the payload field of sources[i].
    // Belt-and-braces - silent OOB if a future retriever change diverges them.
    assert(retrieved.sources.size() == retrieved.texts.size());

    std::string ctx;
    if (!retrieved.sources.empty()) {
        ctx += "Relevant case decisions:\n";
        for (std::size_t i = 0; i < retrieved.sources.size(); ++i) {
            ctx += "\n[S" + std::to_string(i + 1) + "] " + retrieved.texts[i] + "\n";
        }
    }
    if (!anchor.anchor_text.empty()) {
        ctx += "\n\n" + anchor.anchor_text;
    }
    if (!guidance.text.empty()) {
        ctx += "\n\nGuidance:\n" + guidance.text;
    }
    return ctx;
}

// Strip leading "[Key: value]\n" prefix lines added by jurisdiction.preprocess_question()
// before the rewrite/retrieval step. Verbatim port of core/api.py:_strip_context_prefixes
// (which uses regex r"^(\[[^\]]+\]\s*\n+)+"). Hand-rolled here to avoid pulling RE2
// into this TU - input is short and the pattern is simple.
//
// Zone prefixes bias vector retrieval toward planning/RMA sections instead of building
// law. The full prefixed question is still sent to the LLM for generation - this strip
// only affects the rewrite + retrieval path.
std::string strip_context_prefixes(std::string s) {
    std::size_t pos = 0;
    while (pos < s.size() && s[pos] == '[') {
        const auto end = s.find(']', pos + 1);
        if (end == std::string::npos) break;
        auto after = end + 1;
        // Skip horizontal whitespace, then require at least one newline.
        while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) ++after;
        if (after >= s.size() || s[after] != '\n') break;
        while (after < s.size() && s[after] == '\n') ++after;
        pos = after;
    }
    return pos > 0 ? s.substr(pos) : std::move(s);
}

// Single-shot LLM call that rewrites a question into a form optimised for
// vector retrieval. Verbatim port of core/api.py:_rewrite_query.
//
// Takes a dedicated Generator (constructed in main() with rewrite_max_tokens
// / rewrite_temperature) so the rewrite path doesn't reserve a 2500-token KV
// cache per request and rewrites stay deterministic at T=0.0.
//
// Returns the rewritten question on success; falls back to the input on any
// LLM error, empty response, or jurisdiction opt-out (rewrite_prompt() == "").
// Never propagates exceptions - the rewrite is best-effort and a degraded
// retrieval path is better than a 5xx for the user.
drogon::Task<std::string> rewrite_query(
    std::string                       question,
    const astraea::JurisdictionBase&  jurisdiction,
    astraea::Generator&               rewrite_gen)
{
    const auto custom = jurisdiction.rewrite_prompt();
    if (custom.has_value() && custom->empty()) {
        // Explicit jurisdiction-level opt-out.
        co_return question;
    }
    // Avoid value_or here: std::optional<T>::value_or returns T by-value and
    // would copy DEFAULT_REWRITE_PROMPT into a temporary on every request.
    const std::string& system_prompt =
        custom.has_value() ? *custom : astraea::DEFAULT_REWRITE_PROMPT;

    std::vector<astraea::ChatMessage> msgs{
        {"system", system_prompt},
        {"user",   question},
    };

    try {
        auto rewritten = co_await rewrite_gen.generate(std::move(msgs));
        const auto first = rewritten.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) co_return question;
        const auto last = rewritten.find_last_not_of(" \t\r\n");
        co_return rewritten.substr(first, last - first + 1);
    } catch (const std::exception& e) {
        SPDLOG_WARN("rewrite_query failed, using original question: {}", e.what());
        co_return question;
    }
}

drogon::Task<AssembledRequest> assemble_request(
    std::string                          question,
    astraea::RAGPipeline&                pipeline,
    astraea::Generator&                  rewrite_gen,
    astraea::VectorStore*                leg_store,
    const astraea::JurisdictionBase&     jurisdiction,
    const astraea::RouteTable&           table)
{
    // 1. Strip [Key: value] zone prefixes from the rewrite/retrieval input.
    //    The ORIGINAL `question` (with prefixes intact) is preserved for the
    //    final LLM message - the model needs that context for generation.
    const std::string stripped = strip_context_prefixes(question);

    AssembledRequest req;

    // 2. Optional LLM query rewrite. Falls back to `stripped` on any failure.
    auto t0 = req.timer.now();
    std::string retrieval_q = co_await rewrite_query(stripped, jurisdiction, rewrite_gen);
    req.timer.record("rewrite_ms", t0);

    // 3. Build the route decision ONCE for the whole pipeline. retrieve_anchor,
    //    augment_case_retrieval, and retrieve_manual_guidance all need the
    //    same (question, retrieval_q) decision; computing it here and threading
    //    a pointer down skips 3 redundant AC scans per request (Python has the
    //    same redundancy in core/anchor.py - intentional dedupe here, not a
    //    behaviour divergence). Inputs: original `question` + rewritten
    //    `retrieval_q` - matches Python core/api.py:483 exactly.
    const auto route_dec = build_route_decision(question, retrieval_q, table);

    // 4. Corpus retrieve and anchor retrieve are independent I/O-bound Qdrant
    //    calls with no ordering dependency - run them concurrently. Anchor only
    //    needs retrieval_q; it does not depend on the corpus result. Guidance
    //    still runs sequentially after both because it needs existing_ids
    //    (deduplication against corpus + anchor sources).
    t0 = req.timer.now();
    auto [retrieved, anchor] = co_await astraea::detail::when_all_pair(
        pipeline.retrieve(retrieval_q),
        astraea::retrieve_anchor(
            retrieval_q, /*original_question=*/question,
            pipeline, leg_store, jurisdiction, table, &route_dec));
    req.timer.record("retrieve_ms", t0);
    req.timer.record_ms("embed_ms",  retrieved.embed_ms);
    req.timer.record_ms("anchor_ms", anchor.elapsed_ms);

    // Supplementary case retrieval extends retrieved in-place. Runs after
    // when_all so corpus result is available; anchor is done by then too.
    co_await astraea::augment_case_retrieval(
        question, retrieval_q, pipeline, table,
        retrieved.texts, retrieved.sources, &route_dec);

    // Confidence-gated second retrieval pass: if the augmented corpus is
    // still "low" confidence per jurisdiction.confidence_config(), re-issue
    // retrieval against both the original and rewritten questions with
    // relaxed parameters (top_k=8, min_score=0.65, min_chunks=1) so context
    // the rewriter dropped has a chance to surface. Verbatim port of Python
    // core/api.py:421-428 / core/anchor.py:_refine_retrieve.
    //
    // Computed inline rather than promoted to a helper because the level
    // logic is 4 lines and the only call site is here.
    if (!retrieved.sources.empty()) {
        const auto& cc = jurisdiction.confidence_config();
        float top = 0.0f;
        for (const auto& s : retrieved.sources) {
            if (s.score > top) top = s.score;
        }
        const auto n = static_cast<int>(retrieved.sources.size());
        const bool is_high   = top >= cc.high_score   && n >= cc.high_n;
        const bool is_medium = top >= cc.medium_score && n >= cc.medium_n;
        if (!is_high && !is_medium) {
            // Level == "low". Re-sort cap at 6 is inside refine_retrieve.
            SPDLOG_DEBUG("/ask: low-confidence retrieval (top={:.3f}, n={}); refining", top, n);
            co_await astraea::refine_retrieve(
                question, retrieval_q, pipeline,
                retrieved.texts, retrieved.sources);
        }
    } else {
        // Zero sources: refine against both queries to give the user something
        // to work with rather than failing the whole request. Python's
        // _confidence treats n==0 as "low" too.
        SPDLOG_DEBUG("/ask: empty retrieval; refining");
        co_await astraea::refine_retrieve(
            question, retrieval_q, pipeline,
            retrieved.texts, retrieved.sources);
    }

    std::unordered_set<std::string> existing_ids;
    for (const auto& s : retrieved.sources) existing_ids.insert(s.id);
    for (const auto& s : anchor.leg_sources) existing_ids.insert(s.id);

    t0 = req.timer.now();
    auto guidance = co_await astraea::retrieve_manual_guidance(
        retrieval_q, /*original_question=*/question, pipeline, existing_ids,
        jurisdiction, table, &route_dec);
    req.timer.record("guidance_ms", t0);

    t0 = req.timer.now();
    req.messages.push_back({"system", jurisdiction.system_prompt()});
    std::string user_msg =
        build_context_block(retrieved, anchor, guidance) + "\n\nQuestion: " + question;
    req.context_chars  = static_cast<int>(user_msg.size());
    req.context_chunks = static_cast<int>(retrieved.sources.size()
                                        + anchor.leg_sources.size()
                                        + (guidance.source ? 1u : 0u));
    req.messages.push_back({"user", std::move(user_msg)});
    req.timer.record("context_ms", t0);

    req.sources          = std::move(retrieved.sources);
    req.guidance_source  = guidance.source;
    req.rewritten_q      = (retrieval_q != stripped) ? retrieval_q : std::string{};
    req.leg_sources      = std::move(anchor.leg_sources);
    req.matched_intents  = route_dec.matched_intents;
    req.route_triggered  = route_dec.triggered;
    co_return req;
}

SourceJson to_source_json(const astraea::QdrantPoint& pt,
                          const astraea::JurisdictionBase& jurisdiction) {
    return SourceJson{
        pt.id,
        pt.score,
        jurisdiction.format_source_label(pt.payload),
    };
}

// Real /ask handler — non-streaming. Coroutine-style so the multi-stage
// retrieve / anchor / guidance / generate chain reads sequentially.
//
// Captures pipeline + leg_store + jurisdiction + table by reference; all
// live for the lifetime of drogon::app().run() (constructed once in main()).
drogon::Task<drogon::HttpResponsePtr> ask_handler(
    drogon::HttpRequestPtr               req,
    astraea::RAGPipeline&                pipeline,
    astraea::Generator&                  rewrite_gen,
    astraea::CoordinatorClient*          llm_sem,
    int                                  llm_acquire_timeout_s,
    IpLimiter*                           ip_lim,
    astraea::VectorStore*                leg_store,
    const astraea::JurisdictionBase&     jurisdiction,
    const astraea::RouteTable&           table,
    astraea::SessionStore*               session_store,
    astraea::JsonlWriter*                question_log,
    astraea::JsonlWriter*                route_debug_log,
    const astraea::HealthProber&         health_prober)
{
    const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));

#ifdef ASTRAEA_ENABLE_TIMING
    const auto t_sanitize = std::chrono::steady_clock::now();
#endif
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) {
        err->addHeader("X-Request-Id", req_id);
        co_return err;
    }
#ifdef ASTRAEA_ENABLE_TIMING
    const double sanitize_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t_sanitize).count() / 1000.0;
#endif

    const std::string session_id = req->getHeader("x-session-id");
    const bool use_session = session_store &&
        astraea::SessionStore::valid_session_id(session_id);

    IpLimiter::Permit ip_permit;
    if (ip_lim) {
        const std::string client_ip = req->getPeerAddr().toIp();
        auto maybe_ip = ip_lim->try_acquire(client_ip);
        if (!maybe_ip) {
            SPDLOG_WARN("/ask[{}]: per-IP limit hit for {}", req_id, client_ip);
            auto r = text_response(
                drogon::k429TooManyRequests,
                "Too many concurrent requests from your IP. Please retry.\n");
            r->addHeader("X-Request-Id", req_id);
            co_return r;
        }
        ip_permit = std::move(*maybe_ip);
    }

    // Per-request LLM readiness check (Python core/api.py:_check_llm parity).
    // Returns a friendlier 503 with the "model is loading" hint during
    // llama-server cold-load instead of letting the user hit the more
    // generic upstream-unavailable error several seconds later when
    // generate() actually fails. ~5-20ms on the localhost happy path.
    if (!co_await health_prober.probe_llm()) {
        SPDLOG_INFO("/ask[{}]: LLM /v1/models probe failed, returning 503", req_id);
        auto r = text_response(drogon::k503ServiceUnavailable,
            "The AI model is currently loading. Please try again in 30 seconds.\n");
        r->addHeader("X-Request-Id", req_id);
        co_return r;
    }

    AssembledRequest assembled;
    try {
        assembled = co_await assemble_request(
            question, pipeline, rewrite_gen, leg_store, jurisdiction, table);
#ifdef ASTRAEA_ENABLE_TIMING
        assembled.timer.record_ms("sanitize_ms", sanitize_ms);
#endif
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic so internal URLs /
        // model names / collection names do not leak to clients.
        SPDLOG_WARN("/ask[{}]: retrieval failed: {}", req_id, e.what());
        auto r = text_response(drogon::k503ServiceUnavailable,
            "Upstream retrieval temporarily unavailable\n");
        r->addHeader("X-Request-Id", req_id);
        co_return r;
    }

    std::vector<astraea::ChatMessage> history;
    if (use_session) {
        history = co_await session_store->load(session_id);
        if (!history.empty()) {
            // Inject only the tail (default last 3 turn pairs = 6 messages).
            // load() returns the full stored window so we can round-trip it
            // back to Redis on save without losing older turns. The slice
            // here is purely about prompt budget - older pairs would push
            // TTFT up linearly with session depth otherwise.
            const auto inject_n = static_cast<std::size_t>(
                session_store->inject_message_count());
            auto first = history.size() > inject_n
                ? history.end() - static_cast<std::ptrdiff_t>(inject_n)
                : history.begin();
            assembled.messages.insert(assembled.messages.begin() + 1,
                                      first, history.end());
        }
    }

    const bool log = !is_no_log(req);
    if (log && question_log) {
        QuestionLogEntry qle{utc_iso8601(), req_id, question};
        std::string qle_json;
        if (!glz::write_json(qle, qle_json))
            question_log->append(qle_json);
    }

    std::string answer;
    astraea::CoordinatorClient::Permit gen_permit;
    if (llm_sem) {
        // Acquire the global LLM permit with a configurable timeout (Python
        // core/api.py global_llm_acquire(timeout=90.0) parity). Returning
        // 503 on timeout caps queue pile-up under sustained overload - one
        // slow LLM call away from a death spiral otherwise.
        auto t_wait = assembled.timer.now();
        if (llm_acquire_timeout_s > 0) {
            auto maybe = co_await llm_sem->acquire(
                std::chrono::seconds(llm_acquire_timeout_s));
            assembled.timer.record("llm_wait_ms", t_wait);
            if (!maybe) {
                SPDLOG_WARN("/ask[{}]: LLM permit timeout after {}s (backend={}, max={})",
                            req_id, llm_acquire_timeout_s,
                            llm_sem->backend_name(), llm_sem->max_concurrency());
                auto r = text_response(drogon::k503ServiceUnavailable,
                    "Server is busy. Please retry in a moment.\n");
                r->addHeader("X-Request-Id", req_id);
                co_return r;
            }
            gen_permit = std::move(*maybe);
        } else {
            gen_permit = co_await llm_sem->acquire();
            assembled.timer.record("llm_wait_ms", t_wait);
        }
    }
    auto t_gen = assembled.timer.now();
    try {
        answer = co_await pipeline.generator().generate(std::move(assembled.messages));
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic.
        SPDLOG_WARN("/ask[{}]: generation failed: {}", req_id, e.what());
        auto r = text_response(drogon::k503ServiceUnavailable,
            "Upstream LLM temporarily unavailable\n");
        r->addHeader("X-Request-Id", req_id);
        co_return r;
    }
    assembled.timer.record("generation_ms", t_gen);

    if (use_session) {
        history.push_back({"user",      question});
        history.push_back({"assistant", answer});
        co_await session_store->save(session_id, std::move(history));
    }

    if (log && route_debug_log) {
        RouteDebugEntry rde;
        rde.ts       = utc_iso8601();
        rde.request_id = req_id;
        rde.q        = question;
        rde.rewritten = assembled.rewritten_q;
        rde.triggered = assembled.route_triggered;
        rde.matched_intents = assembled.matched_intents;
        for (const auto& s : assembled.sources)
            rde.sources.push_back({s.id, s.score});
        for (const auto& s : assembled.leg_sources)
            rde.legislation.push_back({s.id, s.payload.count("title")
                ? s.payload.at("title") : std::string{}});
        rde.answer = answer.substr(0, 8000);
        rde.context_chars  = assembled.context_chars;
        rde.context_chunks = assembled.context_chunks;
        std::string rde_json;
        if (!glz::write_json(rde, rde_json))
            route_debug_log->append(rde_json);
    }

    AskResponse out;
    out.answer = std::move(answer);
    out.sources.reserve(assembled.sources.size());
    for (const auto& s : assembled.sources)
        out.sources.push_back(to_source_json(s, jurisdiction));
    if (assembled.guidance_source)
        out.guidance_source = to_source_json(*assembled.guidance_source, jurisdiction);

    std::string body;
    if (auto e = glz::write_json(out, body); e) {
        auto r = text_response(drogon::k500InternalServerError, "Serialization error\n");
        r->addHeader("X-Request-Id", req_id);
        co_return r;
    }
    auto resp = json_response(drogon::k200OK, std::move(body));
    resp->addHeader("X-Request-Id", req_id);
    co_return resp;
}

// Real /ask/stream handler — SSE-shaped streaming via Generator::generate_stream.
//
// Frame format:
//   event: sources                    (one event, fired before any tokens)
//   data: [{"id":..,"label":..,"score":..}, ...]
//
//   data: {"token":"..."}             (one frame per token)
//
//   data: [DONE]                      (terminator)
//
// On retrieval failure: data: {"error":"..."} + [DONE], then close.
//
// IMPORTANT — Phase 3 streaming caveat (PR #9 Generator::generate_stream):
// sendRequestCoro buffers the full LLM response before invoking the
// TokenCallback. Tokens fire in batch AFTER the LLM finishes, not in
// real time. User-perceived UX: a long wait followed by a fast blast.
// Phase 6D will swap to true streaming once Drogon's chunked-body
// coroutine API is in. The shape above is what the client should see
// once true streaming lands, so the frontend can be coded against it
// today without rework.
void ask_stream_handler(
    const drogon::HttpRequestPtr&           req,
    std::function<void(const drogon::HttpResponsePtr&)>&& cb,
    astraea::RAGPipeline&                   pipeline,
    astraea::Generator&                     rewrite_gen,
    astraea::CoordinatorClient*             llm_sem,
    int                                     llm_acquire_timeout_s,
    IpLimiter*                              ip_lim,
    astraea::VectorStore*                   leg_store,
    const astraea::JurisdictionBase&        jurisdiction,
    const astraea::RouteTable&              table,
    astraea::SessionStore*                  session_store,
    astraea::JsonlWriter*                   question_log,
    astraea::JsonlWriter*                   route_debug_log,
    const astraea::HealthProber&            health_prober)
{
    const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));

#ifdef ASTRAEA_ENABLE_TIMING
    const auto t_sanitize = std::chrono::steady_clock::now();
#endif
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) {
        err->addHeader("X-Request-Id", req_id);
        cb(err);
        return;
    }
#ifdef ASTRAEA_ENABLE_TIMING
    const double sanitize_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t_sanitize).count() / 1000.0;
#endif

    IpLimiter::Permit ip_permit;
    if (ip_lim) {
        const std::string client_ip = req->getPeerAddr().toIp();
        auto maybe_ip = ip_lim->try_acquire(client_ip);
        if (!maybe_ip) {
            SPDLOG_WARN("/ask/stream[{}]: per-IP limit hit for {}", req_id, client_ip);
            auto r = text_response(
                drogon::k429TooManyRequests,
                "Too many concurrent requests from your IP. Please retry.\n");
            r->addHeader("X-Request-Id", req_id);
            cb(r);
            return;
        }
        ip_permit = std::move(*maybe_ip);
    }

    std::string sess_id;
    if (session_store) {
        const std::string raw = req->getHeader("x-session-id");
        if (astraea::SessionStore::valid_session_id(raw)) sess_id = raw;
    }

    const bool log = !is_no_log(req);
    if (log && question_log) {
        QuestionLogEntry qle{utc_iso8601(), req_id, question};
        std::string qle_json;
        if (!glz::write_json(qle, qle_json))
            question_log->append(qle_json);
    }

    auto ip_permit_ptr = std::make_shared<IpLimiter::Permit>(std::move(ip_permit));
    auto resp = drogon::HttpResponse::newAsyncStreamResponse(
        [q = std::move(question), &pipeline, &rewrite_gen, llm_sem, llm_acquire_timeout_s,
         ip_permit_ptr, leg_store, &jurisdiction, &table,
         sess_id = std::move(sess_id), session_store, log, route_debug_log,
         &health_prober,
#ifdef ASTRAEA_ENABLE_TIMING
         req_id, sanitize_ms](
#else
         req_id](
#endif
            drogon::ResponseStreamPtr stream) mutable {
            drogon::async_run(
                [stream = std::move(stream), q = std::move(q), &pipeline, &rewrite_gen,
                 llm_sem, llm_acquire_timeout_s, ip_permit_ptr,
                 leg_store, &jurisdiction, &table,
                 sess_id = std::move(sess_id), session_store,
                 &health_prober,
#ifdef ASTRAEA_ENABLE_TIMING
                 log, route_debug_log, req_id, sanitize_ms]() mutable -> drogon::Task<> {
#else
                 log, route_debug_log, req_id]() mutable -> drogon::Task<> {
#endif
                    // ResponseStreamPtr is std::unique_ptr<ResponseStream>; the
                    // TokenCallback below cannot copy it. Convert ownership to a
                    // shared_ptr so both this coroutine and the callback hold
                    // owning refs - survives Phase 6D's switch to async chunked
                    // streaming where the callback may fire from a different
                    // coroutine frame than the one currently suspended.
                    auto shared_stream = std::shared_ptr<drogon::ResponseStream>(
                        std::move(stream));

                    // Capture the event loop that owns this client connection
                    // before any co_await that may shift the coroutine to a
                    // different trantor I/O thread. Used by pin_to_loop below.
                    auto* client_loop =
                        trantor::EventLoop::getEventLoopOfCurrentThread();

                    // Defensive send wrapper: ResponseStream::send() returns
                    // false when the underlying TCP connection has been closed
                    // (browser tab closed, peer RST mid-stream, etc). Calling
                    // send() after that point doesn't crash by itself, but it
                    // widens the race window for the trantor Channel::remove
                    // assertion (events_ != kNoneEvent) when the LLM token
                    // callback - which runs on a different event-loop thread
                    // than the response stream's owning loop - races with
                    // Drogon's connection teardown. Two mitigations bundled:
                    //   1. Short-circuit further sends after the first false
                    //      return - stops feeding a dead pipe.
                    //   2. Log a single INFO line per disconnect so production
                    //      operators can correlate trantor crash bursts with
                    //      client-side disconnect rates.
                    // When peer_dead is set, generate_stream passes it to
                    // LlmStreamSession as a cancellation flag. The session
                    // aborts the upstream LLM connection on the next token,
                    // releasing the global semaphore without waiting for [DONE].
                    //
                    // NOTE: the LLM token callback (further down) does NOT
                    // call safe_send - it checks peer_dead and calls
                    // shared_stream->send() inline to avoid std::function
                    // indirection at per-token call rates. It sets peer_dead
                    // on the same false-return path so the two sites stay in
                    // sync. Grep for `peer disconnected mid-stream` in logs
                    // - that one phrase covers both call sites.
                    auto peer_dead = std::make_shared<std::atomic<bool>>(false);
                    auto safe_send = [shared_stream, peer_dead, req_id](
                        const std::string& data) -> bool {
                        if (peer_dead->load(std::memory_order_relaxed)) return false;
                        if (!shared_stream->send(data)) {
                            if (!peer_dead->exchange(true, std::memory_order_relaxed))
                                SPDLOG_INFO("/ask/stream[{}]: peer disconnected mid-stream", req_id);
                            return false;
                        }
                        return true;
                    };

                    // Per-request LLM readiness check (Python core/api.py:_check_llm parity).
                    // Returns a friendlier SSE error during llama-server cold-load
                    // instead of letting the user hit a generic upstream error
                    // 200-2000ms later when generate_stream actually fails.
                    // Probe is ~5-20ms on the localhost happy path.
                    if (!co_await health_prober.probe_llm()) {
                        SPDLOG_INFO("/ask/stream[{}]: LLM /v1/models probe failed, returning SSE error", req_id);
                        // probe_llm internally co_awaits an HTTP call that may
                        // resume on a non-client loop; re-pin before send.
                        co_await astraea::detail::pin_to_loop(client_loop);
                        safe_send("data: {\"error\":\"The AI model is currently loading. "
                                  "Please try again in 30 seconds.\"}\n\n");
                        safe_send("data: {\"type\":\"done\"}\n\n");
                        shared_stream->close();
                        co_return;
                    }

                    // assemble_request reuses 6C.2's coroutine - retrieve,
                    // anchor, guidance, build [system, user] messages.
                    // Uses when_all_pair/async_run internally so it may resume
                    // on a different event-loop thread. We must not co_await
                    // inside the catch block (C++ restriction), so use a flag
                    // and handle the error after the pin_to_loop below.
                    AssembledRequest assembled;
                    bool retrieval_ok = true;
                    try {
                        assembled = co_await assemble_request(
                            q, pipeline, rewrite_gen, leg_store, jurisdiction, table);
#ifdef ASTRAEA_ENABLE_TIMING
                        assembled.timer.record_ms("sanitize_ms", sanitize_ms);
#endif
                    } catch (const std::exception& e) {
                        // Detail in the log only - client sees a generic error.
                        SPDLOG_WARN("/ask/stream[{}]: retrieval failed: {}", req_id, e.what());
                        retrieval_ok = false;
                    }
                    // Re-pin #1: assemble_request (when_all_pair) may have
                    // shifted us to another loop. Pin before safe_send so the
                    // sources event and error sends are on client_loop.
                    co_await astraea::detail::pin_to_loop(client_loop);
                    if (!retrieval_ok) {
                        safe_send("data: {\"error\":\"upstream retrieval temporarily unavailable\"}\n\n");
                        safe_send("data: {\"type\":\"done\"}\n\n");
                        shared_stream->close();
                        co_return;
                    }

                    std::vector<astraea::ChatMessage> history;
                    if (session_store && !sess_id.empty()) {
                        history = co_await session_store->load(sess_id);
                        if (!history.empty()) {
                            const auto inject_n = static_cast<std::size_t>(
                                session_store->inject_message_count());
                            auto first = history.size() > inject_n
                                ? history.end() - static_cast<std::ptrdiff_t>(inject_n)
                                : history.begin();
                            assembled.messages.insert(
                                assembled.messages.begin() + 1,
                                first, history.end());
                        }
                    }

                    // Emit a sources event upfront so the client renders
                    // citation chips even before the first token arrives.
                    // Includes guidance_source as a sibling field so the SSE
                    // contract has full parity with /ask's AskResponse.
                    SourcesEvent srcs_ev;
                    srcs_ev.sources.reserve(assembled.sources.size());
                    for (const auto& s : assembled.sources)
                        srcs_ev.sources.push_back(to_source_json(s, jurisdiction));
                    for (const auto& s : assembled.leg_sources)
                        srcs_ev.legislation.push_back({
                            s.payload.count("case_id") ? s.payload.at("case_id") : s.id,
                            s.payload.count("title")   ? s.payload.at("title")   : "",
                            s.payload.count("url")     ? s.payload.at("url")     : "",
                        });
                    if (assembled.guidance_source)
                        srcs_ev.guidance_source =
                            to_source_json(*assembled.guidance_source, jurisdiction);

                    std::string srcs_body;
                    if (auto e = glz::write_json(srcs_ev, srcs_body); !e) {
                        safe_send("data: " + srcs_body + "\n\n");
                    } else {
                        SPDLOG_WARN("/ask/stream[{}]: sources serialisation failed", req_id);
                    }

                    // Stream tokens via Generator::generate_stream + TokenCallback.
                    // True per-token streaming as of Phase 6D - on_token fires
                    // for each SSE chunk the LLM emits, not in a single batch
                    // after generation completes.
                    astraea::CoordinatorClient::Permit gen_permit;
                    if (llm_sem) {
                        // Acquire the global LLM permit with timeout (Python
                        // core/api.py global_llm_acquire(timeout=90) parity).
                        // On timeout, emit a JSON error event in the SSE stream
                        // rather than 503ing - the headers are already on the
                        // wire by the time this coroutine runs.
                        auto t_wait = assembled.timer.now();
                        if (llm_acquire_timeout_s > 0) {
                            auto maybe = co_await llm_sem->acquire(
                                std::chrono::seconds(llm_acquire_timeout_s));
                            assembled.timer.record("llm_wait_ms", t_wait);
                            if (!maybe) {
                                SPDLOG_WARN("/ask/stream[{}]: LLM permit timeout after {}s "
                                            "(backend={}, max={})",
                                            req_id, llm_acquire_timeout_s,
                                            llm_sem->backend_name(),
                                            llm_sem->max_concurrency());
                                // Re-pin before emitting + closing - the acquire()
                                // coroutine may have left us on a non-client loop,
                                // and we don't fall through to Re-pin #2 on this
                                // early-return path. Same race the assemble_request
                                // catch path closes via Re-pin #1.
                                co_await astraea::detail::pin_to_loop(client_loop);
                                safe_send(
                                    "data: {\"error\":\"server is busy, please retry\"}\n\n");
                                safe_send("data: {\"type\":\"done\"}\n\n");
                                shared_stream->close();
                                co_return;
                            }
                            gen_permit = std::move(*maybe);
                        } else {
                            gen_permit = co_await llm_sem->acquire();
                            assembled.timer.record("llm_wait_ms", t_wait);
                        }
                    }
                    // Re-pin #2: session_store->load() and llm_sem->acquire()
                    // may have drifted the coroutine off client_loop. Pin back
                    // so generate_stream is created on L_client; token callbacks
                    // then fire on the same thread as TCP teardown, eliminating
                    // the Channel::remove() race (events_ != kNoneEvent assert).
                    co_await astraea::detail::pin_to_loop(client_loop);

                    std::string full_answer;
                    bool first_token = true;
                    auto t_gen = assembled.timer.now();
                    try {
                        co_await pipeline.generator().generate_stream(
                            std::move(assembled.messages),
                            // req_id captured BY VALUE here. The reference
                            // would be valid today (outer-lambda owns the
                            // std::string, coroutine frame stays alive for
                            // the duration of co_await), but the invariant
                            // is non-obvious: any future refactor that
                            // hoists generate_stream out of co_await, or
                            // changes the outer lambda's capture mode,
                            // turns &req_id into a dangling reference.
                            // String is small, copy is cheap, value avoids
                            // the footgun.
                            [shared_stream, peer_dead, &full_answer, &first_token,
                             &assembled, &t_gen, req_id](std::string_view token) {
                                // NOTE: peer_dead is also the cancellation
                                // flag passed to generate_stream below. When
                                // send() returns false here and peer_dead is
                                // set to true, LlmStreamSession sees it on
                                // the next token and calls finish() to abort
                                // the upstream LLM connection.
                                if (peer_dead->load(std::memory_order_relaxed)) return;
                                if (first_token) {
                                    assembled.timer.record("ttft_ms", t_gen);
                                    first_token = false;
                                }
                                full_answer.append(token);
                                std::string body;
                                if (auto e = glz::write_json(
                                        TokenChunk{"token", std::string(token)}, body); e) {
                                    SPDLOG_WARN("/ask/stream[{}]: token serialisation failed",
                                                req_id);
                                    return;
                                }
                                if (!shared_stream->send("data: " + body + "\n\n")) {
                                    if (!peer_dead->exchange(true, std::memory_order_relaxed))
                                        SPDLOG_INFO("/ask/stream[{}]: peer disconnected mid-stream",
                                                    req_id);
                                }
                            },
                            peer_dead);
                    } catch (const std::exception& e) {
                        SPDLOG_WARN("/ask/stream[{}]: generation failed: {}", req_id, e.what());
                        safe_send(
                            "data: {\"error\":\"upstream LLM temporarily unavailable\"}\n\n");
                    }
                    assembled.timer.record("generation_ms", t_gen);

                    // Skip session save if the client disconnected mid-stream:
                    // the conversation is incomplete and the peer is gone.
                    if (session_store && !sess_id.empty() && !full_answer.empty()
                            && !peer_dead->load(std::memory_order_relaxed)) {
                        history.push_back({"user",      q});
                        history.push_back({"assistant", full_answer});
                        co_await session_store->save(sess_id, std::move(history));
                    }

#ifdef ASTRAEA_ENABLE_TIMING
                    // Emit timing SSE event (compile-time opt-in via ASTRAEA_ENABLE_TIMING).
                    {
                        TimingEvent te;
                        te.request_id    = req_id;
                        te.sanitize_ms   = assembled.timer.agg({"sanitize_ms"});
                        te.rewrite_ms    = assembled.timer.agg({"rewrite_ms"});
                        te.embed_ms      = assembled.timer.agg({"embed_ms"});
                        te.retrieve_ms   = assembled.timer.agg({"retrieve_ms"});
                        te.anchor_ms     = assembled.timer.agg({"anchor_ms"});
                        te.guidance_ms   = assembled.timer.agg({"guidance_ms"});
                        te.context_ms    = assembled.timer.agg({"context_ms"});
                        te.llm_wait_ms   = assembled.timer.agg({"llm_wait_ms"});
                        te.ttft_ms       = assembled.timer.agg({"ttft_ms"});
                        te.generation_ms = assembled.timer.agg({"generation_ms"});
                        te.total_ms      = assembled.timer.elapsed_ms();
                        te.context_chars  = assembled.context_chars;
                        te.context_chunks = assembled.context_chunks;
                        te.detail        = assembled.timer.steps();
                        std::string te_json;
                        if (!glz::write_json(te, te_json))
                            safe_send("data: " + te_json + "\n\n");
                    }
#endif // ASTRAEA_ENABLE_TIMING

                    if (log && route_debug_log && !full_answer.empty()) {
                        RouteDebugEntry rde;
                        rde.ts            = utc_iso8601();
                        rde.request_id    = req_id;
                        rde.q             = q;
                        rde.rewritten     = assembled.rewritten_q;
                        rde.triggered     = assembled.route_triggered;
                        rde.matched_intents = assembled.matched_intents;
                        for (const auto& s : assembled.sources)
                            rde.sources.push_back({s.id, s.score});
                        for (const auto& s : assembled.leg_sources)
                            rde.legislation.push_back({s.id,
                                s.payload.count("title")
                                    ? s.payload.at("title") : std::string{}});
                        rde.answer = full_answer.substr(0, 8000);
                        rde.context_chars  = assembled.context_chars;
                        rde.context_chunks = assembled.context_chunks;
                        std::string rde_json;
                        if (!glz::write_json(rde, rde_json))
                            route_debug_log->append(rde_json);
                    }

                    // Emit legislation-grounded verification event using the
                    // legislation sections already retrieved by retrieve_anchor.
                    // This replaces the Playwright web_verify from Python v1 -
                    // the Qdrant corpus text is the same source the answer is
                    // grounded in, so we show it directly without HTTP scraping.
                    if (!assembled.leg_sources.empty()) {
                        VerificationEvent vev;
                        vev.sections.reserve(assembled.leg_sources.size());
                        for (const auto& s : assembled.leg_sources) {
                            VerificationSection vs;
                            vs.url = s.payload.count("url")   ? s.payload.at("url")   : "";
                            vs.reference = s.payload.count("title") ? s.payload.at("title") : "";
                            const std::string& txt = s.payload.count("text")
                                ? s.payload.at("text") : std::string{};
                            vs.excerpt = txt.size() > 500 ? txt.substr(0, 500) + "..." : txt;
                            if (!vs.url.empty() || !vs.reference.empty())
                                vev.sections.push_back(std::move(vs));
                        }
                        if (!vev.sections.empty()) {
                            std::string vev_json;
                            if (!glz::write_json(vev, vev_json))
                                safe_send("data: " + vev_json + "\n\n");
                        }
                    }

                    safe_send("data: {\"type\":\"done\"}\n\n");
                    shared_stream->close();
                });
        });
    resp->addHeader("Content-Type",     "text/event-stream");
    resp->addHeader("Cache-Control",    "no-cache");
    resp->addHeader("X-Accel-Buffering", "no");
    resp->addHeader("X-Request-Id",     req_id);
    cb(resp);
}

// POST /feedback — collect thumbs-up/thumbs-down ratings from users.
//
// Request body: {"question":"...","rating":1-5,"comment":"..."}
// rating is required (1=bad, 5=great). question and comment are free text.
// Per-IP rate gate (IpCooldown, 30 s TTL) prevents trivial spam. Responds
// 200 "ok\n" on success; 400 on bad input; 429 on rate limit.
drogon::HttpResponsePtr feedback_handler(
    const drogon::HttpRequestPtr& req,
    astraea::JsonlWriter*         feedback_log,
    astraea::IpCooldown*          feedback_cooldown)
{
    const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));

    auto reply = [&req_id](drogon::HttpStatusCode code, std::string body) {
        auto r = text_response(code, std::move(body));
        r->addHeader("X-Request-Id", req_id);
        return r;
    };

    FeedbackRequest parsed{};
    if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, req->getBody()); err) {
        return reply(drogon::k400BadRequest, "Invalid JSON\n");
    }
    if (parsed.rating < 1 || parsed.rating > 5) {
        return reply(drogon::k400BadRequest, "rating must be 1-5\n");
    }
    if (parsed.question.empty()) {
        return reply(drogon::k400BadRequest, "question is required\n");
    }
    if (feedback_cooldown) {
        const std::string ip = req->getPeerAddr().toIp();
        if (!feedback_cooldown->try_consume(ip)) {
            return reply(drogon::k429TooManyRequests,
                "Feedback rate limit: please wait 30 seconds between submissions.\n");
        }
    }
    if (feedback_log) {
        FeedbackEntry fe{utc_iso8601(), req_id, parsed.question, parsed.rating, parsed.comment};
        std::string fe_json;
        if (!glz::write_json(fe, fe_json))
            feedback_log->append(fe_json);
    }
    return reply(drogon::k200OK, "ok\n");
}

// GET /debug/ping — frontend's debug-mode bootstrap. Returns 200 {"ok":true}
// when X-Debug-Key matches the configured DEBUG_KEY env var; 403 otherwise.
// X-API-Key is ALSO required (the existing auth advice handles that). When
// DEBUG_KEY is unset (empty), every probe gets 403 - debug mode is disabled.
//
// Python parity: core/api.py `@app.get("/debug/ping")`. The frontend (app.js
// _activateDebug) gates the entire debug UI on an OK response from this
// endpoint.
drogon::HttpResponsePtr debug_ping_handler(
    const drogon::HttpRequestPtr& req,
    const std::string&            debug_key)
{
    const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));
    auto reply = [&req_id](drogon::HttpStatusCode code, std::string body) {
        auto r = text_response(code, std::move(body));
        r->addHeader("X-Request-Id", req_id);
        return r;
    };

    if (debug_key.empty()) {
        // No DEBUG_KEY configured -> debug mode disabled. Same response as
        // a wrong key, so an attacker can't tell whether debug is unset
        // vs the key is wrong.
        return reply(drogon::k403Forbidden, "Invalid debug key.\n");
    }
    const auto& provided = req->getHeader("x-debug-key");
    // Constant-time compare for the same reason as PUBLIC_TOKEN (#38).
    if (provided.size() != debug_key.size() ||
        CRYPTO_memcmp(provided.data(), debug_key.data(), debug_key.size()) != 0) {
        return reply(drogon::k403Forbidden, "Invalid debug key.\n");
    }

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody("{\"ok\":true}");
    resp->addHeader("X-Request-Id", req_id);
    return resp;
}

// POST /feedback/full — comprehensive feedback with full request context
// (sources, legislation, timing, debug payloads). Frontend's astraea.js
// `saveFullFeedback` calls this for debug-mode submissions.
//
// The body shape varies by frontend version (~25 typed fields in Python's
// FeedbackFullRequest, several nested dicts/arrays). Single-pass DOM parse:
// read the body as glz::json_t, extract the two fields we validate
// (rating + is_debug), augment with server-side fields, write to JSONL.
// No typed validation struct - that would either need to live outside the
// anonymous namespace (PR #9 glaze-linkage rule) or duplicate the parse.
drogon::HttpResponsePtr feedback_full_handler(
    const drogon::HttpRequestPtr& req,
    const std::string&            jurisdiction_name,
    astraea::JsonlWriter&         feedback_full_log,
    astraea::IpCooldown&          full_cooldown)
{
    const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));
    auto reply = [&req_id](drogon::HttpStatusCode code, std::string body) {
        auto r = text_response(code, std::move(body));
        r->addHeader("X-Request-Id", req_id);
        return r;
    };

    // Single-pass parse of the body as a generic JSON DOM. Reject anything
    // that isn't an object - the frontend always sends one, and an array or
    // scalar would skip our augment-with-server-fields step.
    glz::json_t entry;
    if (auto err = glz::read_json(entry, req->getBody()); err || !entry.is_object()) {
        return reply(drogon::k400BadRequest, "Invalid JSON\n");
    }
    auto& obj = entry.get_object();

    int  rating   = 0;
    bool is_debug = false;
    if (auto it = obj.find("rating"); it != obj.end() && it->second.is_number())
        rating = static_cast<int>(it->second.get<double>());
    if (auto it = obj.find("is_debug"); it != obj.end() && it->second.is_boolean())
        is_debug = it->second.get<bool>();

    // Python: `if not is_debug and rating not in (1, -1)`. Debug-mode
    // submissions bypass the rating check (context-only, not user feedback).
    if (!is_debug && rating != 1 && rating != -1) {
        return reply(drogon::k400BadRequest, "Rating must be 1 or -1.\n");
    }

    // Per-IP rate gate. Python uses 1 s TTL for /feedback/full (vs 30 s
    // for /feedback) - debug mode fires multiple consecutive submissions.
    const std::string ip = req->getPeerAddr().toIp();
    if (!full_cooldown.try_consume(ip)) {
        return reply(drogon::k429TooManyRequests,
            "Feedback rate limit: please wait briefly between submissions.\n");
    }

    // Augment with server-side fields. Frontend can't be trusted to set
    // these (timestamp, request_id, jurisdiction).
    obj["ts"]           = utc_iso8601();
    obj["request_id"]   = req_id;
    obj["jurisdiction"] = jurisdiction_name;

    std::string out;
    if (!glz::write_json(entry, out))
        feedback_full_log.append(out);

    return reply(drogon::k200OK, "ok\n");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // spdlog setup before anything that might log. Pattern includes
    // millisecond timestamps + log level + message. Runtime level can be
    // overridden via LOG_LEVEL env (trace|debug|info|warn|err|critical|off);
    // default is info. Compile-time SPDLOG_ACTIVE_LEVEL (dev preset sets it
    // to DEBUG, prod leaves it at INFO) controls whether SPDLOG_DEBUG calls
    // are emitted at all.
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    if (const char* lvl = std::getenv("LOG_LEVEL")) {
        spdlog::set_level(spdlog::level::from_str(lvl));
    } else {
        spdlog::set_level(spdlog::level::info);
    }

    verify_mimalloc_override();

    const auto cfg = astraea::Config::from_env();
    const astraea::nz_tenancy::NZTenancyJurisdiction jurisdiction;

    // Singletons that live for the lifetime of drogon::app().run() and are
    // captured by reference into the request handlers below. Constructing
    // them here proves the long ctor chains succeed before any traffic
    // arrives — a misconfigured URL or model would fail loudly at startup
    // rather than on the first request.
    astraea::RAGPipeline pipeline{
        cfg.qdrant_url,
        jurisdiction.corpus().qdrant_collection,
        cfg.embed_base_url,
        cfg.embed_model,
        cfg.llm_base_url,
        cfg.llm_model,
        cfg.rerank_base_url,                   // reranker endpoint (default :8081 embed server)
        cfg.rerank_model,                      // reranker model (default bge-m3)
        jurisdiction.corpus().courts.empty()   // VectorStore court_name filter
            ? std::string{}
            : jurisdiction.corpus().courts.front(),
        /*embed_dims=*/               cfg.embed_dims,
        /*llm_max_tokens=*/           cfg.llm_max_tokens,
        /*llm_temperature=*/          cfg.llm_temperature,
        /*enable_reranker=*/          cfg.enable_reranker,
        /*enable_thinking=*/          cfg.enable_thinking,
        /*upstream_timeout_s=*/       static_cast<double>(cfg.upstream_timeout_s),
        /*stream_idle_timeout_s=*/    static_cast<double>(cfg.llm_stream_idle_timeout_s),
    };

    // Dedicated Generator for query rewrite: capped at rewrite_max_tokens (100
    // default) so each /ask request doesn't reserve a 2500-token KV cache for
    // a ~10-20 token rewrite, and pinned to T=0.0 so the same input produces
    // the same retrieval query (defeats the "optimised for retrieval" intent
    // otherwise). Same base_url + model as the generation Generator - hits
    // the same LLM server, just with a tighter request body.
    astraea::Generator rewrite_gen{
        cfg.llm_base_url,
        cfg.llm_model,
        cfg.rewrite_max_tokens,
        cfg.rewrite_temperature,
        /*enable_thinking=*/false,
        /*stream_idle_timeout_s=*/static_cast<double>(cfg.llm_stream_idle_timeout_s),
    };

    // Distributed-permit coordinator that caps concurrent generation calls
    // when LLM_GLOBAL_CONCURRENCY > 0. Wraps the final generate() /
    // generate_stream() only (Python core/api.py:553 parity) - retrieval
    // and rewrite stay parallel. nullptr below means unlimited concurrency.
    //
    // Backend selection via COORDINATOR_BACKEND env var:
    //   "in_process" (default) - single-binary deployments
    //   "redis"                - multi-host / cross-process (PR #34)
    std::unique_ptr<astraea::CoordinatorClient> coordinator;
    if (cfg.llm_global_concurrency > 0) {
        if (cfg.coordinator_backend == "redis") {
            coordinator = std::make_unique<astraea::RedisCoordinator>(
                cfg.redis_url, cfg.llm_global_concurrency);
        } else if (cfg.coordinator_backend == "in_process") {
            coordinator = std::make_unique<astraea::InProcessCoordinator>(
                cfg.llm_global_concurrency);
        } else {
            std::fprintf(stderr,
                "FATAL: COORDINATOR_BACKEND='%s' is unknown; expected "
                "'in_process' or 'redis'.\n",
                cfg.coordinator_backend.c_str());
            std::abort();
        }
    }
    astraea::CoordinatorClient* const llm_sem = coordinator.get();

    // Per-IP concurrency limiter. nullptr when IP_MAX_CONCURRENCY == 0 (unlimited).
    std::optional<IpLimiter> ip_lim_storage;
    if (cfg.ip_max_concurrency > 0)
        ip_lim_storage.emplace(cfg.ip_max_concurrency);
    IpLimiter* const ip_lim = ip_lim_storage ? &*ip_lim_storage : nullptr;

    // Optional separate VectorStore for the legislation collection. The
    // anchor pipeline accepts a nullable pointer; nullptr falls back to
    // single-collection mode (vector store from RAGPipeline).
    std::unique_ptr<astraea::VectorStore> leg_store;
    if (!jurisdiction.corpus().leg_collection.empty()) {
        leg_store = std::make_unique<astraea::VectorStore>(
            cfg.qdrant_url,
            jurisdiction.corpus().leg_collection,
            /*court_name=*/std::string{},
            /*timeout_s=*/static_cast<double>(cfg.upstream_timeout_s));
    }

    // Redis session store. Optional: only constructed when REDIS_URL is set.
    // Keeps turn history per X-Session-Id header across requests so the LLM
    // can refer back to earlier context in the same conversation. Disabled if
    // redis_url is empty (the default). Fail-open: Redis errors never surface
    // to callers - see session.hpp for details.
    std::optional<astraea::SessionStore> session_store_storage;
    if (!cfg.redis_url.empty()) {
        session_store_storage.emplace(
            cfg.redis_url,
            std::string(jurisdiction.name()),
            cfg.session_ttl_s,
            cfg.session_max_turns,
            cfg.session_inject_turns,
            cfg.session_answer_cap);
    }
    astraea::SessionStore* const session_store =
        session_store_storage ? &*session_store_storage : nullptr;

    // JSONL feedback writers. Fail-open: errors are logged and swallowed,
    // never surfaced to callers. All three land in feedback_dir; route_debug
    // gets a larger per-file cap because each entry includes the full answer.
    astraea::JsonlWriter question_log{
        std::filesystem::path(cfg.feedback_dir) / "question_log.jsonl",
        static_cast<std::uintmax_t>(cfg.feedback_max_mb) * 1024ULL * 1024};
    astraea::JsonlWriter route_debug_log{
        std::filesystem::path(cfg.feedback_dir) / "route_debug.jsonl",
        static_cast<std::uintmax_t>(cfg.route_debug_max_mb) * 1024ULL * 1024};
    astraea::JsonlWriter feedback_log{
        std::filesystem::path(cfg.feedback_dir) / "feedback.jsonl",
        static_cast<std::uintmax_t>(cfg.feedback_max_mb) * 1024ULL * 1024};
    // 30 s per-IP cooldown for /feedback to prevent trivial spam.
    astraea::IpCooldown feedback_cooldown{std::chrono::seconds(30)};

    // /feedback/full lands in a separate JSONL because the per-entry size is
    // much larger (full request context, sources, legislation, timing) and
    // the use case is debug-mode capture rather than user-facing feedback.
    // Python ports use feedback_full_max_bytes = 50 MB; we reuse the
    // route_debug_max_mb cap which is also 50 MB by default.
    astraea::JsonlWriter feedback_full_log{
        std::filesystem::path(cfg.feedback_dir) / "feedback_debug.jsonl",
        static_cast<std::uintmax_t>(cfg.route_debug_max_mb) * 1024ULL * 1024};
    // 1 s TTL per Python's feedback_full pattern - lets debug-mode capture
    // multiple consecutive submissions but bounds it.
    astraea::IpCooldown feedback_full_cooldown{std::chrono::seconds(1)};

    // Pre-built RouteTable: AC automaton built once at startup, reused by
    // every request. Live for the process lifetime; safe to capture by
    // const reference into handler lambdas.
    const astraea::RouteTable route_table{jurisdiction.routes()};

    // Deep readiness probe for /healthz. Rerank URL is empty when the
    // reranker is disabled - HealthProber skips that probe in that case.
    astraea::HealthProber health_prober{
        cfg.qdrant_url,
        cfg.llm_base_url,
        cfg.embed_base_url,
        cfg.enable_reranker ? cfg.rerank_base_url : std::string{},
    };

    drogon::app()
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", cfg.port)
        .setThreadNum(cfg.thread_count)
        .setClientMaxBodySize(cfg.max_body_bytes);

    // ---------------------------------------------------------------------------
    // Auth: reject requests missing a valid X-API-Key when PUBLIC_TOKEN is set.
    // OPTIONS preflight is exempt — browsers send it before auth headers are
    // attached, and the CORS spec requires a 200 response for the preflight to
    // proceed. If PUBLIC_TOKEN is empty (default) all requests are allowed.
    // ---------------------------------------------------------------------------
    if (!cfg.public_token.empty()) {
        drogon::app().registerSyncAdvice(
            [token = cfg.public_token](const drogon::HttpRequestPtr& req)
            -> drogon::HttpResponsePtr {
                if (req->method() == drogon::Options) return nullptr;
                const auto& path = req->getPath();
                if (path == "/health" || path == "/healthz") return nullptr;
                if (path == "/token") return nullptr;
                if (path == "/" || path == "/index.html"
                    || path == "/favicon.svg" || path == "/robots.txt") return nullptr;
                if (path.starts_with("/static/")) return nullptr;
                const auto& provided = req->getHeader("x-api-key");
                // Constant-time compare prevents timing side-channel: an
                // attacker measuring response latency cannot recover the
                // token byte-by-byte via std::string::operator!= early-exit.
                // Length mismatch leaks only that the length is wrong, which
                // is acceptable (token length is not a secret).
                const bool ok = provided.size() == token.size() &&
                    CRYPTO_memcmp(provided.data(), token.data(), token.size()) == 0;
                if (!ok) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k401Unauthorized);
                    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
                    resp->setBody("Unauthorized\n");
                    return resp;
                }
                return nullptr;
            });
    }

    // ---------------------------------------------------------------------------
    // CORS: attach Access-Control-* headers to every response so browsers can
    // reach the API from a different origin. ALLOWED_ORIGIN defaults to "*";
    // set it to the exact frontend origin in production.
    // ---------------------------------------------------------------------------
    drogon::app().registerPreSendingAdvice(
        [origin = cfg.allowed_origin](const drogon::HttpRequestPtr&,
                                      const drogon::HttpResponsePtr& resp) {
            // Remove before add: addHeader appends and browsers reject duplicate
            // Access-Control-Allow-Origin values (the others are CSV-mergeable).
            resp->removeHeader("Access-Control-Allow-Origin");
            resp->addHeader("Access-Control-Allow-Origin",  origin);
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers",
                            "Content-Type, X-API-Key, Cache-Control, X-Session-Id, X-No-Log, X-Request-Id, X-Debug-Key");
            // Expose-Headers is the read side: by default XHR/fetch only sees
            // a small whitelist of response headers. X-Request-Id must be
            // exposed explicitly so browser clients can pick it up for
            // correlation / display in error UI.
            resp->addHeader("Access-Control-Expose-Headers", "X-Request-Id");
        });

    drogon::app().registerHandler("/health",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(health_handler());
        }, {drogon::Get});

    // Deep readiness probe. /health stays as the fast liveness check
    // (no upstream calls); /healthz pings every dependency and returns
    // a per-dep JSON status with HTTP 200 (ok) or 503 (any down).
    // Coroutine-via-async_run pattern for the same Drogon FunctionTraits
    // reason as /ask: captured lambdas cannot use the coroutine-style
    // registerHandler overload.
    drogon::app().registerHandler("/healthz",
        [&health_prober, llm_sem](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            drogon::async_run(
                [cb = std::move(cb), &health_prober, llm_sem]()
                -> drogon::Task<> {
                    auto resp = co_await healthz_handler(health_prober, llm_sem);
                    cb(resp);
                });
        }, {drogon::Get});

    // Real /ask handler. The OUTER lambda is callback-style (which Drogon's
    // FunctionTraits accepts with captures); the INNER coroutine via
    // drogon::async_run runs the multi-stage retrieve / anchor / guidance /
    // generate chain. Drogon's coroutine-style registerHandler overload does
    // not accept captured lambdas (FunctionTraits rejects them with
    // "no type named first_param_type"), so we cannot use that form here.
    // Pipeline, leg_store, jurisdiction, and route_table are all stack-bound
    // to drogon::app().run()'s lifetime; capturing by reference is safe.
    drogon::app().registerHandler("/ask",
        [&pipeline, &rewrite_gen, llm_sem, llm_to = cfg.llm_acquire_timeout_s, ip_lim,
         leg_ptr = leg_store.get(), &jurisdiction, &route_table, session_store,
         &question_log, &route_debug_log, &health_prober](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            drogon::async_run(
                [req, cb = std::move(cb), &pipeline, &rewrite_gen, llm_sem, llm_to, ip_lim,
                 leg_ptr, &jurisdiction, &route_table, session_store,
                 &question_log, &route_debug_log, &health_prober]() -> drogon::Task<> {
                    auto resp = co_await ask_handler(
                        req, pipeline, rewrite_gen, llm_sem, llm_to, ip_lim, leg_ptr,
                        jurisdiction, route_table, session_store,
                        &question_log, &route_debug_log, health_prober);
                    cb(resp);
                });
        }, {drogon::Post});

    drogon::app().registerHandler("/ask/stream",
        [&pipeline, &rewrite_gen, llm_sem, llm_to = cfg.llm_acquire_timeout_s, ip_lim,
         leg_ptr = leg_store.get(), &jurisdiction, &route_table, session_store,
         &question_log, &route_debug_log, &health_prober](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            ask_stream_handler(req, std::move(cb), pipeline, rewrite_gen, llm_sem, llm_to,
                                ip_lim, leg_ptr, jurisdiction, route_table, session_store,
                                &question_log, &route_debug_log, health_prober);
        }, {drogon::Post});

    drogon::app().registerHandler("/feedback",
        [&feedback_log, &feedback_cooldown](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(feedback_handler(req, &feedback_log, &feedback_cooldown));
        }, {drogon::Post});

    // /feedback/full: comprehensive frontend debug feedback. Body shape varies
    // by frontend version - we validate rating/is_debug and pass-through the
    // rest as raw JSON, augmented server-side with ts/request_id/jurisdiction.
    drogon::app().registerHandler("/feedback/full",
        [&feedback_full_log, &feedback_full_cooldown,
         jur_name = std::string{jurisdiction.name()}](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(feedback_full_handler(req, jur_name,
                                     feedback_full_log, feedback_full_cooldown));
        }, {drogon::Post});

    // /debug/ping: frontend's debug-mode bootstrap. Returns 200 if the
    // X-Debug-Key header matches Config::debug_key, 403 otherwise. The
    // existing X-API-Key auth advice still gates the call - both headers
    // are required when PUBLIC_TOKEN is set.
    drogon::app().registerHandler("/debug/ping",
        [debug_key = cfg.debug_key](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(debug_ping_handler(req, debug_key));
        }, {drogon::Get});

    // CORS preflight — 200 OK with no body; the pre-sending advice above adds
    // the Access-Control-* headers automatically.
    auto options_cb = [](const drogon::HttpRequestPtr&,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
        cb(drogon::HttpResponse::newHttpResponse());
    };
    drogon::app().registerHandler("/health",        options_cb, {drogon::Options});
    drogon::app().registerHandler("/healthz",       options_cb, {drogon::Options});
    drogon::app().registerHandler("/ask",           options_cb, {drogon::Options});
    drogon::app().registerHandler("/ask/stream",    options_cb, {drogon::Options});
    drogon::app().registerHandler("/feedback",      options_cb, {drogon::Options});
    drogon::app().registerHandler("/feedback/full", options_cb, {drogon::Options});
    drogon::app().registerHandler("/debug/ping",    options_cb, {drogon::Options});

    // /token: returns the public API token so the browser frontend can
    // authenticate subsequent /ask and /ask/stream requests. No auth required
    // (it is itself the bootstrap endpoint that provides the token).
    drogon::app().registerHandler("/token",
        [token = cfg.public_token](
            const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            std::string body;
            (void)glz::write_json(TokenResp{token}, body);
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            resp->setBody(std::move(body));
            cb(resp);
        }, {drogon::Get});

    // Static frontend: serve index.html + /static/* when STATIC_DIR is set.
    // The document root also handles GET / -> index.html automatically.
    // The separate astraea.js lives one level above the jurisdiction static dir
    // (core/frontend/astraea.js), so we register it as a dedicated handler.
    if (!cfg.static_dir.empty()) {
        namespace fs = std::filesystem;
        const fs::path sdir{cfg.static_dir};

        // GET / -> index.html. Registered explicitly because index.html lives
        // inside sdir, but the document root is set to sdir's parent so that
        // /static/* URL prefix maps to the sdir filesystem path.
        const fs::path index_html = sdir / "index.html";
        drogon::app().registerHandler("/",
            [p = index_html.string()](
                const drogon::HttpRequestPtr&,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                cb(drogon::HttpResponse::newFileResponse(p));
            }, {drogon::Get});

        // /static/astraea/astraea.js lives outside sdir (shared core/frontend/).
        const fs::path astraea_js = sdir.parent_path().parent_path().parent_path()
                                    / "core" / "frontend" / "astraea.js";
        if (fs::exists(astraea_js)) {
            const std::string ajs_content = [&]{
                std::ifstream f(astraea_js);
                return std::string(std::istreambuf_iterator<char>(f), {});
            }();
            drogon::app().registerHandler("/static/astraea/astraea.js",
                [body = ajs_content](
                    const drogon::HttpRequestPtr&,
                    std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setContentTypeCode(drogon::CT_TEXT_JAVASCRIPT);
                    resp->setBody(body);
                    cb(resp);
                }, {drogon::Get});
        }

        // Document root = parent of sdir so that /static/* URLs map to
        // sdir/* files (e.g. /static/app.js -> sdir/app.js).
        drogon::app().setDocumentRoot(sdir.parent_path().string());
    }

    LOG_INFO << "jurisdiction: " << jurisdiction.name()
             << " (" << jurisdiction.description() << ")";
    LOG_INFO << "routes:       " << jurisdiction.routes().size()
             << "; leg_sources: " << jurisdiction.leg_sources().size();
    LOG_INFO << "listener:     0.0.0.0:" << cfg.port
             << " (" << cfg.thread_count << " threads, max_body="
             << cfg.max_body_bytes << " bytes)";
    LOG_INFO << "upstreams:";
    LOG_INFO << "  qdrant " << cfg.qdrant_url
             << " (collections: " << jurisdiction.corpus().qdrant_collection
             << (jurisdiction.corpus().leg_collection.empty()
                 ? std::string{}
                 : ", " + jurisdiction.corpus().leg_collection)
             << ")";
    LOG_INFO << "  embed  " << cfg.embed_base_url   << " (" << cfg.embed_model  << ")";
    LOG_INFO << "  rerank " << cfg.rerank_base_url  << " (" << cfg.rerank_model << ")";
    LOG_INFO << "  llm    " << cfg.llm_base_url     << " (" << cfg.llm_model
             << ", thinking=" << (cfg.enable_thinking ? "on" : "off") << ")";
    LOG_INFO << "auth:         "
             << (cfg.public_token.empty() ? "disabled (PUBLIC_TOKEN not set)"
                                          : "X-API-Key required");
    LOG_INFO << "cors:         Access-Control-Allow-Origin: " << cfg.allowed_origin;
    LOG_INFO << "rate_limit:   "
             << (cfg.ip_max_concurrency > 0
                 ? "per-IP max=" + std::to_string(cfg.ip_max_concurrency)
                 : "disabled (IP_MAX_CONCURRENCY=0)");
    if (llm_sem) {
        LOG_INFO << "coordinator:  " << llm_sem->backend_name()
                 << " (max=" << llm_sem->max_concurrency()
                 << ", acquire_timeout=" << cfg.llm_acquire_timeout_s << "s)";
    } else {
        LOG_INFO << "coordinator:  none (LLM_GLOBAL_CONCURRENCY=0; unlimited)";
    }
    if (session_store) {
        LOG_INFO << "session:      redis " << cfg.redis_url
                 << " (ttl=" << cfg.session_ttl_s
                 << "s, max_turns=" << cfg.session_max_turns
                 << ", inject=" << cfg.session_inject_turns
                 << ", answer_cap=" << cfg.session_answer_cap << ")";
    } else {
        LOG_INFO << "session:      disabled (REDIS_URL not set)";
    }
    LOG_INFO << "feedback_dir: " << cfg.feedback_dir
             << " (question_log/route_debug max=" << cfg.feedback_max_mb << "/"
             << cfg.route_debug_max_mb << " MB, feedback max=" << cfg.feedback_max_mb << " MB)";
    LOG_INFO << "endpoints:";
    LOG_INFO << "  GET/OPTIONS  /health        - liveness probe (no upstream checks)";
    LOG_INFO << "  GET/OPTIONS  /healthz       - readiness probe (pings qdrant + llm + embed + rerank)";
    LOG_INFO << "  POST/OPTIONS /ask           - real RAG (retrieve + anchor + guidance + generate)";
    LOG_INFO << "  POST/OPTIONS /ask/stream    - real RAG + SSE token stream (true per-token, Phase 6D)";
    LOG_INFO << "  POST/OPTIONS /feedback      - user rating submission (1-5 stars, 30 s per-IP)";
    LOG_INFO << "  POST/OPTIONS /feedback/full - debug-mode feedback with full context (1 s per-IP)";
    LOG_INFO << "  GET/OPTIONS  /debug/ping    - debug-mode gate ("
             << (cfg.debug_key.empty() ? "DISABLED: DEBUG_KEY not set" : "X-Debug-Key required")
             << ")";

    drogon::app().run();
    return 0;
}
