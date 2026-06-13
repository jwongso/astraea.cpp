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
//   curl -X POST http://localhost:8080/ask -H 'Content-Type: application/json' \
//        -d '{"question":"what is the bond limit?"}'
//   curl -N -X POST http://localhost:8080/ask/stream -H 'Content-Type: application/json' \
//        -d '{"question":"what is the bond limit?"}'

#include <drogon/drogon.h>
#include <drogon/HttpResponse.h>
#include <mimalloc.h>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "astraea/anchor.hpp"
#include "astraea/config.hpp"
#include "astraea/coordinator.hpp"
#include "astraea/detail/when_all.hpp"
#include "astraea/generator.hpp"
#include "astraea/health.hpp"
#include "astraea/in_process_coordinator.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/redis_coordinator.hpp"
#include "astraea/route_table.hpp"
#include "astraea/session.hpp"
#include "astraea/retriever.hpp"
#include "astraea/sanitize.hpp"
#include "nz_tenancy/jurisdiction.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
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

struct TokenChunk {
    std::string token;
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
struct SourcesEvent {
    std::vector<SourceJson> sources;
    std::optional<SourceJson> guidance_source;
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
    if (auto err = glz::read_json(parsed, req->getBody()); err) {
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
    std::vector<astraea::ChatMessage> messages;
    std::vector<astraea::QdrantPoint> sources;
    std::optional<astraea::QdrantPoint> guidance_source;
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

    // 2. Optional LLM query rewrite. Falls back to `stripped` on any failure.
    std::string retrieval_q = co_await rewrite_query(stripped, jurisdiction, rewrite_gen);

    // 3. Corpus retrieve and anchor retrieve are independent I/O-bound Qdrant
    //    calls with no ordering dependency - run them concurrently. Anchor only
    //    needs retrieval_q; it does not depend on the corpus result. Guidance
    //    still runs sequentially after both because it needs existing_ids
    //    (deduplication against corpus + anchor sources).
    auto [retrieved, anchor] = co_await astraea::detail::when_all_pair(
        pipeline.retrieve(retrieval_q),
        astraea::retrieve_anchor(
            retrieval_q, /*original_question=*/question,
            pipeline, leg_store, jurisdiction, table));

    // Supplementary case retrieval extends retrieved in-place. Runs after
    // when_all so corpus result is available; anchor is done by then too.
    co_await astraea::augment_case_retrieval(
        question, retrieval_q, pipeline, table,
        retrieved.texts, retrieved.sources);

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

    auto guidance = co_await astraea::retrieve_manual_guidance(
        retrieval_q, /*original_question=*/question, pipeline, existing_ids, jurisdiction, table);

    AssembledRequest req;
    req.messages.push_back({"system", jurisdiction.system_prompt()});
    req.messages.push_back({"user",
        build_context_block(retrieved, anchor, guidance)
            + "\n\nQuestion: " + question});
    req.sources         = std::move(retrieved.sources);
    req.guidance_source = guidance.source;
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
    astraea::SessionStore*               session_store)
{
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) co_return err;

    const std::string session_id = req->getHeader("x-session-id");
    const bool use_session = session_store &&
        astraea::SessionStore::valid_session_id(session_id);

    IpLimiter::Permit ip_permit;
    if (ip_lim) {
        const std::string client_ip = req->getPeerAddr().toIp();
        auto maybe_ip = ip_lim->try_acquire(client_ip);
        if (!maybe_ip) {
            SPDLOG_WARN("/ask: per-IP limit hit for {}", client_ip);
            co_return text_response(
                drogon::k429TooManyRequests,
                "Too many concurrent requests from your IP. Please retry.\n");
        }
        ip_permit = std::move(*maybe_ip);
    }

    AssembledRequest assembled;
    try {
        assembled = co_await assemble_request(
            question, pipeline, rewrite_gen, leg_store, jurisdiction, table);
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic so internal URLs /
        // model names / collection names do not leak to clients.
        SPDLOG_WARN("/ask: retrieval failed: {}", e.what());
        co_return text_response(drogon::k503ServiceUnavailable,
            "Upstream retrieval temporarily unavailable\n");
    }

    std::vector<astraea::ChatMessage> history;
    if (use_session) {
        history = co_await session_store->load(session_id);
        if (!history.empty())
            assembled.messages.insert(assembled.messages.begin() + 1,
                                      history.begin(), history.end());
    }

    std::string answer;
    astraea::CoordinatorClient::Permit gen_permit;
    if (llm_sem) {
        // Acquire the global LLM permit with a configurable timeout (Python
        // core/api.py global_llm_acquire(timeout=90.0) parity). Returning
        // 503 on timeout caps queue pile-up under sustained overload - one
        // slow LLM call away from a death spiral otherwise.
        if (llm_acquire_timeout_s > 0) {
            auto maybe = co_await llm_sem->acquire(
                std::chrono::seconds(llm_acquire_timeout_s));
            if (!maybe) {
                SPDLOG_WARN("/ask: LLM permit timeout after {}s (backend={}, max={})",
                            llm_acquire_timeout_s,
                            llm_sem->backend_name(), llm_sem->max_concurrency());
                co_return text_response(drogon::k503ServiceUnavailable,
                    "Server is busy. Please retry in a moment.\n");
            }
            gen_permit = std::move(*maybe);
        } else {
            gen_permit = co_await llm_sem->acquire();
        }
    }
    try {
        answer = co_await pipeline.generator().generate(std::move(assembled.messages));
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic.
        SPDLOG_WARN("/ask: generation failed: {}", e.what());
        co_return text_response(drogon::k503ServiceUnavailable,
            "Upstream LLM temporarily unavailable\n");
    }

    if (use_session) {
        history.push_back({"user",      question});
        history.push_back({"assistant", answer});
        co_await session_store->save(session_id, std::move(history));
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
        co_return text_response(drogon::k500InternalServerError, "Serialization error\n");
    }
    co_return json_response(drogon::k200OK, std::move(body));
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
    astraea::SessionStore*                  session_store)
{
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) {
        cb(err);
        return;
    }

    IpLimiter::Permit ip_permit;
    if (ip_lim) {
        const std::string client_ip = req->getPeerAddr().toIp();
        auto maybe_ip = ip_lim->try_acquire(client_ip);
        if (!maybe_ip) {
            SPDLOG_WARN("/ask/stream: per-IP limit hit for {}", client_ip);
            cb(text_response(
                drogon::k429TooManyRequests,
                "Too many concurrent requests from your IP. Please retry.\n"));
            return;
        }
        ip_permit = std::move(*maybe_ip);
    }

    std::string sess_id;
    if (session_store) {
        const std::string raw = req->getHeader("x-session-id");
        if (astraea::SessionStore::valid_session_id(raw)) sess_id = raw;
    }

    auto ip_permit_ptr = std::make_shared<IpLimiter::Permit>(std::move(ip_permit));
    auto resp = drogon::HttpResponse::newAsyncStreamResponse(
        [q = std::move(question), &pipeline, &rewrite_gen, llm_sem, llm_acquire_timeout_s,
         ip_permit_ptr, leg_store, &jurisdiction, &table,
         sess_id = std::move(sess_id), session_store](
            drogon::ResponseStreamPtr stream) mutable {
            drogon::async_run(
                [stream = std::move(stream), q = std::move(q), &pipeline, &rewrite_gen,
                 llm_sem, llm_acquire_timeout_s, ip_permit_ptr,
                 leg_store, &jurisdiction, &table,
                 sess_id = std::move(sess_id), session_store]() mutable -> drogon::Task<> {
                    // ResponseStreamPtr is std::unique_ptr<ResponseStream>; the
                    // TokenCallback below cannot copy it. Convert ownership to a
                    // shared_ptr so both this coroutine and the callback hold
                    // owning refs - survives Phase 6D's switch to async chunked
                    // streaming where the callback may fire from a different
                    // coroutine frame than the one currently suspended.
                    auto shared_stream = std::shared_ptr<drogon::ResponseStream>(
                        std::move(stream));

                    // assemble_request reuses 6C.2's coroutine - retrieve,
                    // anchor, guidance, build [system, user] messages.
                    AssembledRequest assembled;
                    try {
                        assembled = co_await assemble_request(
                            q, pipeline, rewrite_gen, leg_store, jurisdiction, table);
                    } catch (const std::exception& e) {
                        // Detail in the log only - client sees a generic error.
                        SPDLOG_WARN("/ask/stream: retrieval failed: {}", e.what());
                        shared_stream->send(
                            "data: {\"error\":\"upstream retrieval temporarily unavailable\"}\n\n");
                        shared_stream->send("data: [DONE]\n\n");
                        shared_stream->close();
                        co_return;
                    }

                    std::vector<astraea::ChatMessage> history;
                    if (!sess_id.empty()) {
                        history = co_await session_store->load(sess_id);
                        if (!history.empty())
                            assembled.messages.insert(
                                assembled.messages.begin() + 1,
                                history.begin(), history.end());
                    }

                    // Emit a sources event upfront so the client renders
                    // citation chips even before the first token arrives.
                    // Includes guidance_source as a sibling field so the SSE
                    // contract has full parity with /ask's AskResponse.
                    SourcesEvent srcs_ev;
                    srcs_ev.sources.reserve(assembled.sources.size());
                    for (const auto& s : assembled.sources)
                        srcs_ev.sources.push_back(to_source_json(s, jurisdiction));
                    if (assembled.guidance_source)
                        srcs_ev.guidance_source =
                            to_source_json(*assembled.guidance_source, jurisdiction);

                    std::string srcs_body;
                    if (auto e = glz::write_json(srcs_ev, srcs_body); !e) {
                        shared_stream->send("event: sources\ndata: " + srcs_body + "\n\n");
                    } else {
                        SPDLOG_WARN("/ask/stream: sources serialisation failed");
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
                        if (llm_acquire_timeout_s > 0) {
                            auto maybe = co_await llm_sem->acquire(
                                std::chrono::seconds(llm_acquire_timeout_s));
                            if (!maybe) {
                                SPDLOG_WARN("/ask/stream: LLM permit timeout after {}s "
                                            "(backend={}, max={})",
                                            llm_acquire_timeout_s,
                                            llm_sem->backend_name(),
                                            llm_sem->max_concurrency());
                                shared_stream->send(
                                    "data: {\"error\":\"server is busy, please retry\"}\n\n");
                                shared_stream->send("data: [DONE]\n\n");
                                shared_stream->close();
                                co_return;
                            }
                            gen_permit = std::move(*maybe);
                        } else {
                            gen_permit = co_await llm_sem->acquire();
                        }
                    }
                    std::string full_answer;
                    try {
                        co_await pipeline.generator().generate_stream(
                            std::move(assembled.messages),
                            [shared_stream, &full_answer](std::string_view token) {
                                full_answer.append(token);
                                std::string body;
                                if (auto e = glz::write_json(
                                        TokenChunk{std::string(token)}, body); e) {
                                    SPDLOG_WARN("/ask/stream: token serialisation failed");
                                    return;
                                }
                                shared_stream->send("data: " + body + "\n\n");
                            });
                    } catch (const std::exception& e) {
                        SPDLOG_WARN("/ask/stream: generation failed: {}", e.what());
                        shared_stream->send(
                            "data: {\"error\":\"upstream LLM temporarily unavailable\"}\n\n");
                    }

                    if (!sess_id.empty() && !full_answer.empty()) {
                        history.push_back({"user",      q});
                        history.push_back({"assistant", std::move(full_answer)});
                        co_await session_store->save(sess_id, std::move(history));
                    }

                    shared_stream->send("data: [DONE]\n\n");
                    shared_stream->close();
                });
        });
    resp->addHeader("Content-Type",     "text/event-stream");
    resp->addHeader("Cache-Control",    "no-cache");
    resp->addHeader("X-Accel-Buffering", "no");
    cb(resp);
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
        /*embed_dims=*/      cfg.embed_dims,
        /*llm_max_tokens=*/  cfg.llm_max_tokens,
        /*llm_temperature=*/ cfg.llm_temperature,
        /*enable_reranker=*/ cfg.enable_reranker,
        /*enable_thinking=*/ cfg.enable_thinking,
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
            /*court_name=*/std::string{});
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
            cfg.session_max_turns);
    }
    astraea::SessionStore* const session_store =
        session_store_storage ? &*session_store_storage : nullptr;

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
                if (req->getHeader("x-api-key") != token) {
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
                            "Content-Type, X-API-Key, Cache-Control");
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
         leg_ptr = leg_store.get(), &jurisdiction, &route_table, session_store](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            drogon::async_run(
                [req, cb = std::move(cb), &pipeline, &rewrite_gen, llm_sem, llm_to, ip_lim,
                 leg_ptr, &jurisdiction, &route_table, session_store]() -> drogon::Task<> {
                    auto resp = co_await ask_handler(
                        req, pipeline, rewrite_gen, llm_sem, llm_to, ip_lim, leg_ptr,
                        jurisdiction, route_table, session_store);
                    cb(resp);
                });
        }, {drogon::Post});

    drogon::app().registerHandler("/ask/stream",
        [&pipeline, &rewrite_gen, llm_sem, llm_to = cfg.llm_acquire_timeout_s, ip_lim,
         leg_ptr = leg_store.get(), &jurisdiction, &route_table, session_store](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            ask_stream_handler(req, std::move(cb), pipeline, rewrite_gen, llm_sem, llm_to,
                                ip_lim, leg_ptr, jurisdiction, route_table, session_store);
        }, {drogon::Post});

    // CORS preflight — 200 OK with no body; the pre-sending advice above adds
    // the Access-Control-* headers automatically.
    auto options_cb = [](const drogon::HttpRequestPtr&,
                          std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
        cb(drogon::HttpResponse::newHttpResponse());
    };
    drogon::app().registerHandler("/health",     options_cb, {drogon::Options});
    drogon::app().registerHandler("/healthz",    options_cb, {drogon::Options});
    drogon::app().registerHandler("/ask",        options_cb, {drogon::Options});
    drogon::app().registerHandler("/ask/stream", options_cb, {drogon::Options});

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
    LOG_INFO << "  llm    " << cfg.llm_base_url     << " (" << cfg.llm_model    << ")";
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
                 << "s, max_turns=" << cfg.session_max_turns << ")";
    } else {
        LOG_INFO << "session:      disabled (REDIS_URL not set)";
    }
    LOG_INFO << "endpoints:";
    LOG_INFO << "  GET/OPTIONS  /health      - liveness probe (no upstream checks)";
    LOG_INFO << "  GET/OPTIONS  /healthz     - readiness probe (pings qdrant + llm + embed + rerank)";
    LOG_INFO << "  POST/OPTIONS /ask         - real RAG (retrieve + anchor + guidance + generate)";
    LOG_INFO << "  POST/OPTIONS /ask/stream  - real RAG + SSE token stream (true per-token, Phase 6D)";

    drogon::app().run();
    return 0;
}
