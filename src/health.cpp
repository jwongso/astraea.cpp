#include "astraea/health.hpp"

#include <drogon/HttpClient.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <utility>

namespace astraea {

namespace {

// Probe paths per backend kind. Both Qdrant and llama-server (OpenAI-compatible)
// expose at least one cheap 200-OK endpoint we can hit:
//   - Qdrant:        GET /                  (returns version JSON)
//   - llama-server:  GET /v1/models         (returns models list; universal
//                                            across llama-server / vLLM /
//                                            OpenAI-compat servers)
constexpr const char* QDRANT_PROBE_PATH = "/";
constexpr const char* LLM_PROBE_PATH    = "/v1/models";

drogon::HttpClientPtr make_probe_client(const std::string& base_url) {
    // Fresh client per call - lifetime tied to the probe coroutine.
    // Avoids reusing any application client's connection pool, so a probe
    // never observes (or causes) a transient state in the hot-path client.
    return drogon::HttpClient::newHttpClient(base_url);
}

} // namespace

HealthProber::HealthProber(std::string qdrant_url,
                           std::string llm_url,
                           std::string embed_url,
                           std::string rerank_url)
    : _qdrant_url(std::move(qdrant_url))
    , _llm_url(std::move(llm_url))
    , _embed_url(std::move(embed_url))
    , _rerank_url(std::move(rerank_url))
{}

drogon::Task<HealthCheck> HealthProber::probe_one(
    std::string               name,
    const std::string&        base_url,
    const std::string&        path,
    std::chrono::milliseconds timeout) const
{
    HealthCheck check;
    check.name = std::move(name);
    check.url  = base_url;

    const auto start = std::chrono::steady_clock::now();
    try {
        auto client = make_probe_client(base_url);
        if (!client) {
            check.status     = "down";
            check.latency_ms = 0;
            check.error      = "invalid base_url";
            co_return check;
        }
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath(path);

        // Drogon's sendRequestCoro timeout is in seconds (double). Convert
        // from milliseconds with a floor of 1s - sub-second timeouts on a
        // localhost loopback are noisy due to TCP/syscall scheduling jitter.
        const double timeout_sec =
            std::max(1.0, static_cast<double>(timeout.count()) / 1000.0);
        auto resp = co_await client->sendRequestCoro(req, timeout_sec);

        const auto elapsed = std::chrono::steady_clock::now() - start;
        check.latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

        const auto code = static_cast<int>(resp->statusCode());
        if (code == 200) {
            check.status = "ok";
        } else {
            check.status = "down";
            check.error  = "HTTP " + std::to_string(code);
        }
    } catch (const std::exception& e) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check.latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        check.status = "down";
        check.error  = e.what();
        SPDLOG_DEBUG("/healthz probe {} failed: {}", check.name, check.error.value());
    } catch (...) {
        // Non-std exceptions: surface as a generic error instead of crashing
        // the entire /healthz response. Real ASan / UBSan errors still abort
        // because they fire before we get here.
        check.status = "down";
        check.error  = "unknown exception";
    }
    co_return check;
}

drogon::Task<HealthReport> HealthProber::probe(
    std::chrono::milliseconds per_probe_timeout)
{
    HealthReport report;
    report.checks.reserve(4);

    // Sequential probing - keeps lifetime + error handling simple. Total
    // budget under failure is N * timeout; in the typical all-healthy case
    // each probe is single-digit milliseconds on localhost.
    if (!_qdrant_url.empty()) {
        report.checks.push_back(
            co_await probe_one("qdrant", _qdrant_url, QDRANT_PROBE_PATH, per_probe_timeout));
    }
    if (!_llm_url.empty()) {
        report.checks.push_back(
            co_await probe_one("llm", _llm_url, LLM_PROBE_PATH, per_probe_timeout));
    }
    if (!_embed_url.empty()) {
        report.checks.push_back(
            co_await probe_one("embed", _embed_url, LLM_PROBE_PATH, per_probe_timeout));
    }
    if (!_rerank_url.empty()) {
        report.checks.push_back(
            co_await probe_one("rerank", _rerank_url, LLM_PROBE_PATH, per_probe_timeout));
    }

    bool any_down = false;
    for (const auto& c : report.checks) {
        if (c.status == "down") { any_down = true; break; }
    }
    report.overall = any_down ? "down" : "ok";
    co_return report;
}

} // namespace astraea
