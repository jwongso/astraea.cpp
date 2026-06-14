#pragma once
//
// Deep readiness probe for the production app.
//
// Splits cleanly from the existing /health endpoint:
//   - /health  -> liveness. Returns 200 + "ok" with zero dependency checks.
//                 Use for k8s livenessProbe or process supervisors.
//   - /healthz -> readiness. JSON report of every upstream the binary needs:
//                 Qdrant, the LLM chat server, the embed server, the rerank
//                 server (when ENABLE_RERANKER). Returns 200 when all checks
//                 pass, 503 when any required dep is down. Use for k8s
//                 readinessProbe or load-balancer health checks.
//
// The prober is intentionally stateless and fresh - it issues its own HTTP
// requests rather than reusing application clients - so a probe failure
// cannot poison a long-lived shared connection. Per-probe timeout is small
// (default 3 s) so the overall /healthz response time is bounded.
//
// All probes run sequentially. Even at the upper bound this is ~12 s for
// 4 down dependencies; typical all-healthy responses are <200 ms. A future
// PR can parallelise via co_await fanout if probe latency becomes a bottle-
// neck, but for k8s readiness polling (every ~5 s) sequential is fine.
//
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <drogon/utils/coroutine.h>

namespace astraea {

struct HealthCheck {
    std::string                name;        // "qdrant" | "llm" | "embed" | "rerank"
    std::string                status;      // "ok" | "down"
    int                        latency_ms;
    std::string                url;
    std::optional<std::string> error;       // populated iff status == "down"
};

struct CoordinatorInfo {
    std::string backend;                    // e.g. "in_process"
    int         max_concurrency;
};

struct HealthReport {
    // "ok" iff every check is "ok"; "down" if any required check is "down".
    // ("degraded" is reserved for future use when optional deps fail.)
    std::string                    overall;
    std::vector<HealthCheck>       checks;
    std::optional<CoordinatorInfo> coordinator;
};

class HealthProber {
public:
    // Each URL is the base of an OpenAI-compatible LLM server (with /v1/...)
    // or, for qdrant_url, the Qdrant REST root (no /v1 prefix). Empty values
    // skip that probe entirely (used when enable_reranker == false to skip
    // the reranker check).
    HealthProber(std::string qdrant_url,
                 std::string llm_url,
                 std::string embed_url,
                 std::string rerank_url);

    drogon::Task<HealthReport> probe(
        std::chrono::milliseconds per_probe_timeout = std::chrono::seconds(3));

    // Lightweight LLM-only readiness check for the request hot path
    // (top of /ask and /ask/stream). Hits GET /v1/models on the LLM URL
    // with a tight 3 s timeout - matches Python core/api.py:_check_llm()
    // verbatim. Returns true on HTTP 200, false on anything else
    // (timeout, connection refused, non-200 status).
    //
    // Cheap on the happy path (~5-20 ms localhost), but adds up if called
    // unconditionally per request. Callers are expected to short-circuit
    // when llm_url is empty (probe is disabled) and may layer a TTL cache
    // on top for high-QPS scenarios. Python re-probes per request and
    // we match that for parity.
    drogon::Task<bool> probe_llm(
        std::chrono::milliseconds timeout = std::chrono::seconds(3)) const;

private:
    drogon::Task<HealthCheck> probe_one(
        std::string                name,
        const std::string&         base_url,
        const std::string&         path,
        std::chrono::milliseconds  timeout) const;

    std::string _qdrant_url;
    std::string _llm_url;
    std::string _embed_url;
    std::string _rerank_url;
};

} // namespace astraea
