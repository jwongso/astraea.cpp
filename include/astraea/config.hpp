#pragma once
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace astraea {

/// @brief All runtime configuration knobs for an astraea app instance.
///
/// Fields are populated by from_env() from environment variables. Defaults
/// are chosen to match the Python reference implementation (core/api.py) so
/// a fresh deployment with no env vars set works against a localhost
/// llama-server + Qdrant stack. Call validate() (or use from_env() which
/// calls it automatically) to catch misconfigured values at startup before
/// any request is handled.
struct Config {
    std::string llm_base_url        = "http://localhost:8080/v1"; ///< Base URL of the OpenAI-compatible LLM server (env: LLM_BASE_URL).
    std::string embed_base_url      = "http://localhost:8081/v1"; ///< Base URL of the embeddings server (env: EMBED_BASE_URL).
    std::string qdrant_url          = "http://localhost:6333"; ///< Qdrant REST root URL, no /v1 prefix (env: QDRANT_URL).
    std::string redis_url           = "redis://127.0.0.1:6379/0"; ///< Redis connection URL for SessionStore and RedisCoordinator (env: REDIS_URL).
    std::string public_token; ///< Bearer token required on /ask endpoints; empty disables auth (env: PUBLIC_TOKEN).
    std::string debug_key; ///< Secret enabling debug SSE events when matched by AskRequest.debug_key (env: DEBUG_KEY).
    std::string allowed_origin      = "*"; ///< Value for the Access-Control-Allow-Origin response header (env: ALLOWED_ORIGIN).
    // Strict-Transport-Security: max-age in seconds. 0 (default) disables
    // the header entirely; set to a non-zero value (typical: 31536000 = 1 yr)
    // only when this binary is fronted by TLS (Cloudflare Tunnel, nginx, etc.).
    // Sending HSTS over plain HTTP is silently ignored by browsers but still
    // misleading in logs, so we default to off.
    int    hsts_max_age_s           = 0; ///< HSTS max-age in seconds; 0 disables the Strict-Transport-Security header (env: HSTS_MAX_AGE_S).
    std::string llm_model           = "qwen3"; ///< Model name forwarded in every chat-completions request (env: LLM_MODEL).
    std::string embed_model         = "BAAI/bge-m3"; ///< Model name forwarded in every embeddings request (env: EMBED_MODEL).
    // Embedding dimensionality MUST match the Qdrant collection vector size.
    // bge-m3 = 1024; bge-base / bge-small = 768. Wrong value -> Qdrant rejects
    // search vectors at runtime.
    int    embed_dims               = 1024; ///< Embedding vector length; must match the Qdrant collection's configured size (env: EMBED_DIMS).
    // Reranker uses the embed server by default (same llama-server binary
    // running bge-m3 with --reranking enabled). Override with RERANK_BASE_URL
    // to point at a dedicated reranker instance.
    std::string rerank_base_url     = "http://localhost:8081/v1"; ///< Base URL of the reranker endpoint; defaults to the embed server (env: RERANK_BASE_URL).
    std::string rerank_model        = "BAAI/bge-m3"; ///< Model name forwarded in every rerank request (env: RERANK_MODEL).
    int    llm_max_tokens           = 2500; ///< Maximum output tokens for main generation; increase for verbose answers (env: LLM_MAX_TOKENS).
    float  llm_temperature          = 0.2f; ///< Sampling temperature for main generation; 0.0-2.0 (env: LLM_TEMPERATURE).
    // Query rewrite uses its own LLM budget: ~10-20 tokens out, deterministic.
    // Decoupled from llm_max_tokens / llm_temperature so generation can stay
    // creative (T=0.2) and verbose (2500) without bloating the rewrite path,
    // which would otherwise reserve a 2500-token KV cache per /ask request.
    int    rewrite_max_tokens       = 100; ///< Maximum output tokens for query rewrite; kept small because the output is a single question (env: REWRITE_MAX_TOKENS).
    float  rewrite_temperature      = 0.0f; ///< Sampling temperature for query rewrite; 0.0 for deterministic output (env: REWRITE_TEMPERATURE).
    int    llm_global_concurrency   = 0; ///< Maximum simultaneous LLM generation calls; 0 disables the semaphore entirely (env: LLM_GLOBAL_CONCURRENCY).
    // Acquire timeout for the global LLM permit (Python core/api.py
    // global_llm_acquire(timeout=90.0) parity). 0 = no timeout (block forever
    // when the LLM is saturated - the legacy behaviour from PR #22).
    // > 0 = return 503 if no permit available within this many seconds.
    int    llm_acquire_timeout_s    = 90; ///< Seconds to wait for a global LLM permit before returning 503; 0 blocks indefinitely (env: LLM_ACQUIRE_TIMEOUT_S).
    // CoordinatorClient backend for the LLM permit. "in_process" (default)
    // is the single-binary AsyncSemaphore; "redis" coordinates across all
    // processes sharing the same Redis instance. The env var dispatches in
    // main(); the field is the parsed/normalised result. `redis_url` (above)
    // is the connection target when this == "redis".
    std::string coordinator_backend = "in_process"; ///< LLM-permit coordination backend: "in_process" or "redis" (env: COORDINATOR_BACKEND).
    // Max concurrent in-flight requests per source IP. 0 = unlimited.
    // Default 3 matches Python core/api.py PER_IP_MAX. Behind a reverse
    // proxy / Cloudflare Tunnel the peer IP is the edge address; set 0
    // and use proxy-level rate limiting instead.
    int    ip_max_concurrency       = 3; ///< Max simultaneous in-flight /ask requests per source IP; 0 disables the per-IP gate (env: IP_MAX_CONCURRENCY).
    bool   enable_reranker          = true; ///< Enable cross-encoder reranking after vector search; disable to reduce latency at some quality cost (env: ENABLE_RERANKER).
    // Forward chat_template_kwargs.enable_thinking on generation requests.
    //
    // Default: false. Matches Python core/generator.py:28 (thinking=False
    // default) and core/api.py:131 (rewrite path explicit False). The
    // earlier default of `true` was the primary suspect for the LLM-phase
    // TTFT regression documented in BENCHMARK_PERF.md - Qwen3 with thinking
    // enabled prepends a <think>...</think> block of 500-3000+ tokens
    // before the first user-visible token, inflating both ttft_ms and
    // generation_ms. The bench showed 8.9x slower TTFT (12,717 vs 1,429 ms)
    // vs Python; this default flip is the headline fix for that gap.
    //
    // Set true (via ENABLE_THINKING=true) to opt in for jurisdictions or
    // queries where the answer-quality boost outweighs the latency cost.
    // Set false (the default) for non-Qwen3 backends that reject unknown
    // chat_template_kwargs fields too.
    bool   enable_thinking          = false; ///< Forward enable_thinking=true in Qwen3 chat_template_kwargs; false avoids the <think> block overhead (env: ENABLE_THINKING).
    // Session store (Redis). 0 TTL disables expiry (not recommended).
    // max_turns caps how many user+assistant pairs are kept per session.
    // inject_turns caps how many of those pairs are actually prepended to
    // the LLM prompt - older turns stay in Redis for transcript purposes
    // but never bloat the prompt. answer_cap truncates each stored answer
    // so the prompt cannot grow unbounded with verbose generations.
    // Defaults match Python core/session.py (3 / 400) - injecting all 10
    // full-length turns has been measured to add 4-8 s TTFT in long chats.
    int    session_ttl_s            = 3600; ///< Redis TTL in seconds for stored session turns; must be > 0 (env: SESSION_TTL_S).
    int    session_max_turns        = 10; ///< Maximum user+assistant pairs kept per session before the oldest are evicted (env: SESSION_MAX_TURNS).
    int    session_inject_turns     = 3; ///< How many of the stored turn pairs are prepended to the LLM prompt; older pairs are kept in Redis only (env: SESSION_INJECT_TURNS).
    int    session_answer_cap       = 400; ///< Assistant messages are truncated to this many chars when saved, preventing prompt bloat; 0 disables (env: SESSION_ANSWER_CAP).
    // HTTP request timeout for one-shot upstream calls: embed server, Qdrant
    // search/fetch, and reranker. 0 disables the timeout (Drogon default).
    int    upstream_timeout_s        = 30; ///< Timeout in seconds for embed, Qdrant, and reranker HTTP calls; 0 disables (env: UPSTREAM_TIMEOUT_S).
    // Idle timeout for the LLM streaming connection. Aborts if no data arrives
    // for this many seconds - covers both initial connect and mid-stream stalls.
    // 0 disables. 300 s is generous for a 2500-token answer at typical speed.
    int    llm_stream_idle_timeout_s = 300; ///< Idle timeout in seconds for the LLM streaming connection; aborts if no token arrives within this window (env: LLM_STREAM_IDLE_TIMEOUT_S).
    // JSONL log output directory. question_log, route_debug, feedback files
    // are written here. Relative to the working directory of the process.
    std::string feedback_dir        = "data"; ///< Directory for JSONL log files (question_log, route_debug, feedback); relative to process working directory (env: FEEDBACK_DIR).
    std::string static_dir; ///< Document root for the frontend static file server; empty disables static serving (env: STATIC_DIR).
    int    feedback_max_mb          = 20; ///< Per-file rotation limit for feedback.jsonl in megabytes (env: FEEDBACK_MAX_MB).
    int    route_debug_max_mb       = 50; ///< Per-file rotation limit for route_debug.jsonl in megabytes (env: ROUTE_DEBUG_MAX_MB).
    int    port                     = 8080; ///< TCP port the Drogon HTTP server listens on; must be 1-65535 (env: PORT).
    // 0 = std::thread::hardware_concurrency() at runtime (resolved in from_env).
    // Drogon spawns one event loop per thread; size to fit the runtime's cores.
    int    thread_count             = 0; ///< Drogon event-loop thread count; 0 resolves to hardware_concurrency() at startup (env: THREAD_COUNT).
    // Maximum request body size in bytes. ~16 KB is well above the 1200-char
    // sanitize ceiling; rejects multi-MB JSON before the parser sees it.
    int    max_body_bytes           = 16 * 1024; ///< Maximum allowed request body size in bytes; requests exceeding this are rejected before parsing (env: MAX_BODY_BYTES).

    /// @brief Construct a Config populated from environment variables.
    ///
    /// Parsing is strict: non-numeric values for integer/float fields fall
    /// back to the compiled-in default and emit a warning to stderr.
    /// thread_count=0 is resolved to hardware_concurrency() (or 4 as a
    /// fallback) before returning. Calls validate() before returning; throws
    /// std::runtime_error on any invalid combination.
    /// @return A fully validated Config ready for use.
    static Config from_env() {
        Config c;
        auto get = [](const char* k, const std::string& def) -> std::string {
            const char* v = std::getenv(k);
            return v ? v : def;
        };
        auto get_int = [](const char* k, int def) -> int {
            const char* v = std::getenv(k);
            if (!v) return def;
            const char* end = v + std::strlen(v);
            int out = def;
            auto [p, ec] = std::from_chars(v, end, out);
            // Require the entire value (after optional trailing whitespace) to parse.
            // Without this, PORT=80abc silently became 80.
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
            if (ec != std::errc{} || p != end) {
                std::fprintf(stderr, "warn: %s=%s is not an integer, using %d\n", k, v, def);
                return def;
            }
            return out;
        };
        auto get_float = [](const char* k, float def) -> float {
            const char* v = std::getenv(k);
            if (!v) return def;
            char* end;
            errno = 0;
            const float f = std::strtof(v, &end);
            // Require the entire value (after optional trailing whitespace) to parse.
            // Without this, LLM_TEMPERATURE=0.5xyz silently became 0.5.
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
            if (errno || end == v || *end != '\0') {
                std::fprintf(stderr, "warn: %s=%s is not a float, using %g\n", k, v, static_cast<double>(def));
                return def;
            }
            return f;
        };
        auto get_bool = [](const char* k, bool def) -> bool {
            const char* v = std::getenv(k);
            if (!v) return def;
            std::string s(v);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (s == "1" || s == "true"  || s == "yes") return true;
            if (s == "0" || s == "false" || s == "no")  return false;
            std::fprintf(stderr, "warn: %s=%s is not a boolean (use 1/true/yes or 0/false/no), using %s\n",
                         k, v, def ? "true" : "false");
            return def;
        };
        c.llm_base_url      = get("LLM_BASE_URL",      c.llm_base_url);
        c.embed_base_url    = get("EMBED_BASE_URL",     c.embed_base_url);
        c.qdrant_url        = get("QDRANT_URL",         c.qdrant_url);
        c.redis_url         = get("REDIS_URL",          c.redis_url);
        c.public_token      = get("PUBLIC_TOKEN",       c.public_token);
        c.debug_key         = get("DEBUG_KEY",          c.debug_key);
        c.allowed_origin    = get("ALLOWED_ORIGIN",     c.allowed_origin);
        c.hsts_max_age_s    = get_int("HSTS_MAX_AGE_S", c.hsts_max_age_s);
        c.llm_model         = get("LLM_MODEL",          c.llm_model);
        c.embed_model       = get("EMBED_MODEL",        c.embed_model);
        c.embed_dims        = get_int("EMBED_DIMS",     c.embed_dims);
        c.rerank_base_url   = get("RERANK_BASE_URL",    c.rerank_base_url);
        c.rerank_model      = get("RERANK_MODEL",       c.rerank_model);
        c.llm_max_tokens    = get_int("LLM_MAX_TOKENS", c.llm_max_tokens);
        c.llm_temperature   = get_float("LLM_TEMPERATURE", c.llm_temperature);
        c.rewrite_max_tokens  = get_int("REWRITE_MAX_TOKENS",  c.rewrite_max_tokens);
        c.rewrite_temperature = get_float("REWRITE_TEMPERATURE", c.rewrite_temperature);
        c.llm_global_concurrency = get_int("LLM_GLOBAL_CONCURRENCY", c.llm_global_concurrency);
        c.llm_acquire_timeout_s = get_int("LLM_ACQUIRE_TIMEOUT_S",   c.llm_acquire_timeout_s);
        c.coordinator_backend = get("COORDINATOR_BACKEND", c.coordinator_backend);
        c.ip_max_concurrency    = get_int("IP_MAX_CONCURRENCY",       c.ip_max_concurrency);
        c.enable_reranker   = get_bool("ENABLE_RERANKER",  c.enable_reranker);
        c.enable_thinking   = get_bool("ENABLE_THINKING",  c.enable_thinking);
        c.session_ttl_s         = get_int("SESSION_TTL_S",         c.session_ttl_s);
        c.session_max_turns     = get_int("SESSION_MAX_TURNS",     c.session_max_turns);
        c.session_inject_turns  = get_int("SESSION_INJECT_TURNS",  c.session_inject_turns);
        c.session_answer_cap    = get_int("SESSION_ANSWER_CAP",    c.session_answer_cap);
        c.upstream_timeout_s        = get_int("UPSTREAM_TIMEOUT_S",        c.upstream_timeout_s);
        c.llm_stream_idle_timeout_s = get_int("LLM_STREAM_IDLE_TIMEOUT_S", c.llm_stream_idle_timeout_s);
        c.feedback_dir      = get("FEEDBACK_DIR",       c.feedback_dir);
        c.static_dir        = get("STATIC_DIR",         c.static_dir);
        c.feedback_max_mb   = get_int("FEEDBACK_MAX_MB",   c.feedback_max_mb);
        c.route_debug_max_mb = get_int("ROUTE_DEBUG_MAX_MB", c.route_debug_max_mb);
        c.port              = get_int("PORT",           c.port);
        c.thread_count      = get_int("THREAD_COUNT",   c.thread_count);
        c.max_body_bytes    = get_int("MAX_BODY_BYTES", c.max_body_bytes);
        // Resolve thread_count=0 to hardware_concurrency at startup, after env
        // overrides have been applied. Falls back to 4 if the runtime cannot
        // determine the count (returns 0 on some restricted environments).
        if (c.thread_count <= 0) {
            const unsigned hw = std::thread::hardware_concurrency();
            c.thread_count = (hw > 0) ? static_cast<int>(hw) : 4;
        }
        c.validate();
        return c;
    }

    /// @brief Validate all fields and throw on the first detected constraint violation.
    ///
    /// Throws std::runtime_error with a human-readable message naming the
    /// offending field and its required range. from_env() always calls this
    /// before returning, so startup fails fast rather than misbehaving at
    /// request time.
    void validate() const {
        auto die = [](const char* msg) { throw std::runtime_error(msg); };
        if (port < 1 || port > 65535)
            die("PORT must be 1-65535");
        if (embed_dims <= 0)
            die("EMBED_DIMS must be > 0");
        if (llm_max_tokens <= 0)
            die("LLM_MAX_TOKENS must be > 0");
        if (llm_temperature < 0.0f || llm_temperature > 2.0f)
            die("LLM_TEMPERATURE must be 0.0-2.0");
        if (rewrite_max_tokens <= 0)
            die("REWRITE_MAX_TOKENS must be > 0");
        if (rewrite_temperature < 0.0f || rewrite_temperature > 2.0f)
            die("REWRITE_TEMPERATURE must be 0.0-2.0");
        if (llm_global_concurrency < 0)
            die("LLM_GLOBAL_CONCURRENCY must be >= 0");
        if (llm_acquire_timeout_s < 0)
            die("LLM_ACQUIRE_TIMEOUT_S must be >= 0");
        if (ip_max_concurrency < 0)
            die("IP_MAX_CONCURRENCY must be >= 0");
        if (session_ttl_s <= 0)
            die("SESSION_TTL_S must be > 0 (Redis SETEX rejects 0 or negative)");
        if (session_max_turns <= 0)
            die("SESSION_MAX_TURNS must be > 0");
        if (session_inject_turns < 0)
            die("SESSION_INJECT_TURNS must be >= 0 (0 disables injection)");
        if (session_inject_turns > session_max_turns)
            die("SESSION_INJECT_TURNS must be <= SESSION_MAX_TURNS");
        if (session_answer_cap < 0)
            die("SESSION_ANSWER_CAP must be >= 0 (0 disables truncation)");
        if (upstream_timeout_s < 0)
            die("UPSTREAM_TIMEOUT_S must be >= 0");
        if (llm_stream_idle_timeout_s < 0)
            die("LLM_STREAM_IDLE_TIMEOUT_S must be >= 0");
        if (feedback_max_mb <= 0)
            die("FEEDBACK_MAX_MB must be > 0");
        if (route_debug_max_mb <= 0)
            die("ROUTE_DEBUG_MAX_MB must be > 0");
        if (max_body_bytes <= 0)
            die("MAX_BODY_BYTES must be > 0");
        if (hsts_max_age_s < 0)
            die("HSTS_MAX_AGE_S must be >= 0 (0 disables HSTS header entirely)");
    }
};

} // namespace astraea
