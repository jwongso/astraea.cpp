#pragma once
//
// JSONL log writers for question, route-debug, and feedback data.
//
// JsonlWriter: thread-safe append-only JSONL file with size-based rotation.
//   - Rotates when the file exceeds max_bytes: renames .1->.2 ... .(keep-1)->.(keep),
//     then renames the live file to .1 and starts a fresh one.
//   - Errors are logged and silently swallowed (never propagates to callers).
//
// IpCooldown: per-IP rate gate for the /feedback endpoint.
//   - Returns false if the same IP submitted within the TTL window.
//   - Expired entries are evicted lazily on each call.
//
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace astraea {

class JsonlWriter {
public:
    explicit JsonlWriter(std::filesystem::path path,
                         std::uintmax_t        max_bytes = 20ULL * 1024 * 1024,
                         int                   keep      = 5);

    // Append one pre-serialised JSON line (no trailing newline needed).
    // Thread-safe. Never throws.
    void append(const std::string& line) noexcept;

private:
    void rotate_if_needed();

    std::filesystem::path _path;
    std::uintmax_t        _max_bytes;
    int                   _keep;
    std::mutex            _mu;
};

class IpCooldown {
public:
    explicit IpCooldown(std::chrono::seconds ttl) noexcept;

    // Returns true if the IP is allowed (not rate-limited), and records it.
    // Returns false if the same IP called within the TTL window.
    bool try_consume(const std::string& ip);

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::chrono::seconds                      _ttl;
    std::unordered_map<std::string, TimePoint> _last;
    std::mutex                                _mu;
};

} // namespace astraea
