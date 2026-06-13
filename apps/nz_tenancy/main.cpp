// nz_tenancy — Phase 6A skeleton.
//
// Three handlers, no business logic yet — Phase 6B will swap the echo /
// synthetic-stream paths for real RAGPipeline + retrieve_anchor +
// Generator::generate_stream wiring.
//
//   GET  /health      → "ok\n"
//   POST /ask         → sanitize_question() + JSON echo
//   POST /ask/stream  → sanitize_question() + synthetic SSE token stream
//                       (echoes the sanitised question one word per chunk
//                        at 100 ms intervals, then "data: [DONE]\n\n")
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
#include <trantor/net/EventLoop.h>
#include <mimalloc.h>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include "astraea/anchor.hpp"
#include "astraea/config.hpp"
#include "astraea/generator.hpp"
#include "astraea/pipeline.hpp"
#include "astraea/route_table.hpp"
#include "astraea/retriever.hpp"
#include "astraea/sanitize.hpp"
#include "nz_tenancy/jurisdiction.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
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

drogon::Task<AssembledRequest> assemble_request(
    std::string                          question,
    astraea::RAGPipeline&                pipeline,
    astraea::VectorStore*                leg_store,
    const astraea::JurisdictionBase&     jurisdiction,
    const astraea::RouteTable&           table)
{
    // Phase 6C.2 path: no LLM rewrite, so retrieval_question == original_question.
    // Phase 6D will plumb jurisdiction.rewrite_prompt() + Generator::generate
    // and pass the rewritten form here. Until then, anchor and guidance receive
    // `question` as both args so combined_q in allow_section() picks up the
    // user's actual words (matches Python behaviour with skip_rewrite=True).
    auto retrieved = co_await pipeline.retrieve(question);

    auto anchor = co_await astraea::retrieve_anchor(
        question, /*original_question=*/question, pipeline, leg_store, jurisdiction, table);

    std::unordered_set<std::string> existing_ids;
    for (const auto& s : retrieved.sources) existing_ids.insert(s.id);
    for (const auto& s : anchor.leg_sources) existing_ids.insert(s.id);

    auto guidance = co_await astraea::retrieve_manual_guidance(
        question, /*original_question=*/question, pipeline, existing_ids, jurisdiction, table);

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
    astraea::VectorStore*                leg_store,
    const astraea::JurisdictionBase&     jurisdiction,
    const astraea::RouteTable&           table)
{
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) co_return err;

    AssembledRequest assembled;
    try {
        assembled = co_await assemble_request(
            question, pipeline, leg_store, jurisdiction, table);
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic so internal URLs /
        // model names / collection names do not leak to clients.
        SPDLOG_WARN("/ask: retrieval failed: {}", e.what());
        co_return text_response(drogon::k503ServiceUnavailable,
            "Upstream retrieval temporarily unavailable\n");
    }

    std::string answer;
    try {
        answer = co_await pipeline.generator().generate(std::move(assembled.messages));
    } catch (const std::exception& e) {
        // Detail in the log only - 503 body stays generic.
        SPDLOG_WARN("/ask: generation failed: {}", e.what());
        co_return text_response(drogon::k503ServiceUnavailable,
            "Upstream LLM temporarily unavailable\n");
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

// Phase 6A synthetic stream — kept as-is in 6C.2; 6C.3 swaps it for the
// real Generator::generate_stream chain.
drogon::HttpResponsePtr ask_stream_handler(const drogon::HttpRequestPtr& req) {
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) return err;

    auto resp = drogon::HttpResponse::newAsyncStreamResponse(
        [q = std::move(question)](drogon::ResponseStreamPtr stream) {
            drogon::async_run(
                [stream = std::move(stream), q = std::move(q)]() -> drogon::Task<> {
                    auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
                    std::size_t pos = 0;
                    while (pos < q.size()) {
                        std::size_t end = q.find(' ', pos);
                        if (end == std::string::npos) end = q.size();
                        std::string word = q.substr(pos, end - pos);

                        std::string chunk_body;
                        if (auto e = glz::write_json(TokenChunk{std::move(word)},
                                                     chunk_body); !e) {
                            stream->send("data: " + chunk_body + "\n\n");
                        }
                        co_await drogon::sleepCoro(loop, std::chrono::milliseconds(100));
                        pos = end < q.size() ? end + 1 : end;
                    }
                    stream->send("data: [DONE]\n\n");
                    stream->close();
                });
        });
    resp->addHeader("Content-Type",     "text/event-stream");
    resp->addHeader("Cache-Control",    "no-cache");
    resp->addHeader("X-Accel-Buffering", "no");
    return resp;
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
    };

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

    // Pre-built RouteTable: AC automaton built once at startup, reused by
    // every request. Live for the process lifetime; safe to capture by
    // const reference into handler lambdas.
    const astraea::RouteTable route_table{jurisdiction.routes()};

    drogon::app()
        .setLogLevel(trantor::Logger::kInfo)
        .addListener("0.0.0.0", cfg.port)
        .setThreadNum(cfg.thread_count)
        .setClientMaxBodySize(cfg.max_body_bytes);

    drogon::app().registerHandler("/health",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(health_handler());
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
        [&pipeline, leg_ptr = leg_store.get(), &jurisdiction, &route_table](
            const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            drogon::async_run(
                [req, cb = std::move(cb), &pipeline, leg_ptr,
                 &jurisdiction, &route_table]() -> drogon::Task<> {
                    auto resp = co_await ask_handler(
                        req, pipeline, leg_ptr, jurisdiction, route_table);
                    cb(resp);
                });
        }, {drogon::Post});

    drogon::app().registerHandler("/ask/stream",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(ask_stream_handler(req));
        }, {drogon::Post});

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
    LOG_INFO << "endpoints:";
    LOG_INFO << "  GET  /health      - liveness probe";
    LOG_INFO << "  POST /ask         - real RAG (retrieve + anchor + guidance + generate)";
    LOG_INFO << "  POST /ask/stream  - sanitize + synthetic SSE (real streaming -> Phase 6C.3)";

    drogon::app().run();
    return 0;
}
