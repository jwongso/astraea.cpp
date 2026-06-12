#pragma once
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    int    llm_max_tokens           = 2500;
    float  llm_temperature          = 0.2f;
    int    llm_global_concurrency   = 0;
    bool   enable_reranker          = true;
    int    port                     = 8080;

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
        c.llm_max_tokens    = get_int("LLM_MAX_TOKENS", c.llm_max_tokens);
        c.llm_temperature   = get_float("LLM_TEMPERATURE", c.llm_temperature);
        c.llm_global_concurrency = get_int("LLM_GLOBAL_CONCURRENCY", c.llm_global_concurrency);
        c.enable_reranker   = get_bool("ENABLE_RERANKER", c.enable_reranker);
        c.port              = get_int("PORT",           c.port);
        return c;
    }
};

} // namespace astraea
