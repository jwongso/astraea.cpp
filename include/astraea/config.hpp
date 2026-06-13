#pragma once
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace astraea {

struct Config {
    std::string llm_base_url        = "http://localhost:8080/v1";
    std::string embed_base_url      = "http://localhost:8081/v1";
    std::string qdrant_url          = "http://localhost:6333";
    std::string redis_url           = "redis://127.0.0.1:6379/0";
    std::string public_token;
    std::string debug_key;
    std::string allowed_origin      = "*";
    std::string llm_model           = "qwen3";
    std::string embed_model         = "BAAI/bge-m3";
    // Embedding dimensionality MUST match the Qdrant collection vector size.
    // bge-m3 = 1024; bge-base / bge-small = 768. Wrong value -> Qdrant rejects
    // search vectors at runtime.
    int    embed_dims               = 1024;
    // Reranker uses the embed server by default (same llama-server binary
    // running bge-m3 with --reranking enabled). Override with RERANK_BASE_URL
    // to point at a dedicated reranker instance.
    std::string rerank_base_url     = "http://localhost:8081/v1";
    std::string rerank_model        = "BAAI/bge-m3";
    int    llm_max_tokens           = 2500;
    float  llm_temperature          = 0.2f;
    // Query rewrite uses its own LLM budget: ~10-20 tokens out, deterministic.
    // Decoupled from llm_max_tokens / llm_temperature so generation can stay
    // creative (T=0.2) and verbose (2500) without bloating the rewrite path,
    // which would otherwise reserve a 2500-token KV cache per /ask request.
    int    rewrite_max_tokens       = 100;
    float  rewrite_temperature      = 0.0f;
    int    llm_global_concurrency   = 0;
    // Acquire timeout for the global LLM permit (Python core/api.py
    // global_llm_acquire(timeout=90.0) parity). 0 = no timeout (block forever
    // when the LLM is saturated - the legacy behaviour from PR #22).
    // > 0 = return 503 if no permit available within this many seconds.
    int    llm_acquire_timeout_s    = 90;
    bool   enable_reranker          = true;
    // Forward chat_template_kwargs.enable_thinking on generation requests.
    // Set false for non-Qwen3 backends that reject unknown chat_template_kwargs
    // fields or do not implement thinking tokens.
    bool   enable_thinking          = true;
    int    port                     = 8080;
    // 0 = std::thread::hardware_concurrency() at runtime (resolved in from_env).
    // Drogon spawns one event loop per thread; size to fit the runtime's cores.
    int    thread_count             = 0;
    // Maximum request body size in bytes. ~16 KB is well above the 1200-char
    // sanitize ceiling; rejects multi-MB JSON before the parser sees it.
    int    max_body_bytes           = 16 * 1024;

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
            return s != "0" && s != "false" && s != "no";
        };
        c.llm_base_url      = get("LLM_BASE_URL",      c.llm_base_url);
        c.embed_base_url    = get("EMBED_BASE_URL",     c.embed_base_url);
        c.qdrant_url        = get("QDRANT_URL",         c.qdrant_url);
        c.redis_url         = get("REDIS_URL",          c.redis_url);
        c.public_token      = get("PUBLIC_TOKEN",       c.public_token);
        c.debug_key         = get("DEBUG_KEY",          c.debug_key);
        c.allowed_origin    = get("ALLOWED_ORIGIN",     c.allowed_origin);
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
        c.enable_reranker   = get_bool("ENABLE_RERANKER",  c.enable_reranker);
        c.enable_thinking   = get_bool("ENABLE_THINKING",  c.enable_thinking);
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
        return c;
    }
};

} // namespace astraea
