#pragma once
//
// Shared Redis URL parser used by RedisCoordinator and SessionStore.
// Accepts redis://host[:port][/db]. No TLS, no AUTH in v1.
//
#include <stdexcept>
#include <string>
#include <string_view>

namespace astraea::detail {

struct ParsedRedisUrl {
    std::string host;
    int         port = 6379;
    int         db   = 0;
};

inline ParsedRedisUrl parse_redis_url(const std::string& url) {
    constexpr std::string_view scheme = "redis://";
    if (url.rfind(scheme, 0) != 0)
        throw std::invalid_argument("expected redis:// URL, got: " + url);

    auto rest = std::string_view(url).substr(scheme.size());

    int db = 0;
    auto slash = rest.find('/');
    std::string_view authority = (slash == std::string_view::npos)
        ? rest : rest.substr(0, slash);
    if (slash != std::string_view::npos) {
        auto db_str = rest.substr(slash + 1);
        if (!db_str.empty()) {
            int v = 0;
            for (char c : db_str) {
                if (c < '0' || c > '9')
                    throw std::invalid_argument(
                        "non-numeric db in Redis URL: " + url);
                v = v * 10 + (c - '0');
            }
            db = v;
        }
    }

    if (authority.find('@') != std::string_view::npos)
        throw std::invalid_argument(
            "Redis URL with auth (user:pass@host) is not supported in v1: " + url);

    ParsedRedisUrl out;
    out.db = db;
    auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        out.host.assign(authority);
        out.port = 6379;
    } else {
        out.host.assign(authority.substr(0, colon));
        int p = 0;
        for (char c : authority.substr(colon + 1)) {
            if (c < '0' || c > '9')
                throw std::invalid_argument(
                    "non-numeric port in Redis URL: " + url);
            p = p * 10 + (c - '0');
            if (p > 65535)
                throw std::invalid_argument(
                    "port out of range in Redis URL: " + url);
        }
        out.port = p;
    }
    if (out.host.empty())
        throw std::invalid_argument("empty host in Redis URL: " + url);
    return out;
}

} // namespace astraea::detail
