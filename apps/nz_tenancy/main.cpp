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

#include "astraea/config.hpp"
#include "astraea/sanitize.hpp"
#include "jurisdictions/nz_tenancy/jurisdiction.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// JSON shapes — named namespace (NOT anonymous) for glaze reflection.
// Anonymous-namespace types have internal linkage and break glaze's
// glz::detail::external<T>. Pattern established in PR #9.
// ---------------------------------------------------------------------------

namespace astraea::detail::nz_tenancy_app {

struct AskRequest {
    std::string question;
};

struct AskEchoResponse {
    std::string echo;
};

struct TokenChunk {
    std::string token;
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

// Phase 6A echo. Replaced in Phase 6B by RAGPipeline + Generator::generate.
drogon::HttpResponsePtr ask_echo_handler(const drogon::HttpRequestPtr& req) {
    std::string question;
    if (auto err = parse_and_sanitize(req, question)) return err;

    std::string body;
    if (auto e = glz::write_json(AskEchoResponse{std::move(question)}, body); e) {
        return text_response(drogon::k500InternalServerError, "Serialization error\n");
    }
    return json_response(drogon::k200OK, std::move(body));
}

// Phase 6A synthetic stream. Replaced in Phase 6B by Generator::generate_stream.
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
    verify_mimalloc_override();

    const auto cfg = astraea::Config::from_env();
    const astraea::nz_tenancy::NZTenancyJurisdiction jurisdiction;

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

    drogon::app().registerHandler("/ask",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(ask_echo_handler(req));
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
    LOG_INFO << "  GET  /health      - liveness probe";
    LOG_INFO << "  POST /ask         - sanitize + JSON echo (Phase 6A)";
    LOG_INFO << "  POST /ask/stream  - sanitize + synthetic SSE (Phase 6A)";
    LOG_INFO << "Phase 6C will wire RAGPipeline through the request handlers";

    drogon::app().run();
    return 0;
}
