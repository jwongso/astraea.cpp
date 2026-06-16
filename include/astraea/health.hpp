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

/// @brief Result of a single dependency probe issued by HealthProber.
struct HealthCheck {
    std::string                name; ///< Dependency name: "qdrant", "llm", "embed", or "rerank".
    std::string                status; ///< "ok" or "down".
    int                        latency_ms; ///< Round-trip time of the probe request in milliseconds.
    std::string                url; ///< The URL that was probed.
    std::optional<std::string> error; ///< Error message when status is "down"; absent when "ok".
};

/// @brief Coordinator backend metadata included in a HealthReport for /healthz introspection.
struct CoordinatorInfo {
    std::string backend; ///< Backend identifier, e.g. "in_process" or "redis".
    int         max_concurrency; ///< Configured permit count.
};

/// @brief Aggregated health report returned by HealthProber::probe().
struct HealthReport {
    std::string                    overall; ///< "ok" when all checks pass; "down" when any required check fails.
    std::vector<HealthCheck>       checks; ///< Per-dependency probe results.
    std::optional<CoordinatorInfo> coordinator; ///< Coordinator info when a CoordinatorClient is wired in; absent otherwise.
};

/// @brief Async dependency prober that issues live HTTP checks against all upstreams.
///
/// Produces HealthReport for the /healthz endpoint (readiness) while /health
/// remains a lightweight liveness-only probe that does no upstream checks.
/// Each probe issues its own HTTP request from a fresh HttpClient so a probe
/// failure cannot poison a long-lived shared connection.
class HealthProber {
public:
    /// @brief Construct with the base URLs of all upstreams to probe.
    ///
    /// Each URL is either the Qdrant REST root (no /v1 prefix) or the base
    /// of an OpenAI-compatible server (/v1/models is appended). Pass an empty
    /// string for any URL that should be skipped (e.g. rerank_url when
    /// enable_reranker is false).
    HealthProber(std::string qdrant_url,
                 std::string llm_url,
                 std::string embed_url,
                 std::string rerank_url);

    /// @brief Run all configured probes sequentially and return an aggregated HealthReport.
    ///
    /// Returns HTTP 200 JSON when overall=="ok", 503 otherwise. All probes run
    /// sequentially; per_probe_timeout bounds each individual call.
    drogon::Task<HealthReport> probe(
        std::chrono::milliseconds per_probe_timeout = std::chrono::seconds(3));

    /// @brief Lightweight LLM-only readiness check for the request hot path.
    ///
    /// Hits GET /v1/models on the LLM URL with a tight timeout. Matches Python
    /// core/api.py:_check_llm() verbatim. Returns true on HTTP 200, false on
    /// anything else. Callers should short-circuit when llm_url is empty.
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
