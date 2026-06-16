#pragma once
//
// JSONL log writers for question, route-debug, and feedback data.
//
// JsonlWriter: async append-only JSONL file with size-based rotation.
//   - Enqueues lines from any thread; a single background thread owns all I/O.
//   - Bounded queue (queue_cap lines): excess entries are dropped and counted.
//   - Destructor drains the queue before joining the worker (shutdown safety).
//   - Rotates when the file exceeds max_bytes: renames .1->.2 ... .(keep-1)->.(keep),
//     then renames the live file to .1 and starts a fresh one.
//   - Errors are logged and silently swallowed (never propagates to callers).
//
// IpCooldown: per-IP rate gate for the /feedback endpoint.
//   - Returns false if the same IP submitted within the TTL window.
//   - Expired entries are evicted lazily on each call.
//
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace astraea {

/// @brief Async append-only JSONL file writer with size-based rotation.
///
/// Lines are enqueued from any thread; a single background thread owns all
/// I/O. The queue is bounded (queue_cap lines) so callers never block; excess
/// entries are counted as drops. The destructor drains the queue before joining
/// the worker. Rotation: when the file exceeds max_bytes, older files are
/// renamed (.1 -> .2, etc.) up to `keep` copies, then the live file is renamed
/// to .1 and a fresh one is opened. Errors are logged and swallowed.
class JsonlWriter {
public:
    explicit JsonlWriter(std::filesystem::path path,
                         std::uintmax_t        max_bytes = 20ULL * 1024 * 1024,
                         int                   keep      = 5,
                         std::size_t           queue_cap = 4096);
    ~JsonlWriter();

    JsonlWriter(const JsonlWriter&)            = delete;
    JsonlWriter& operator=(const JsonlWriter&) = delete;
    JsonlWriter(JsonlWriter&&)                 = delete;
    JsonlWriter& operator=(JsonlWriter&&)      = delete;

    /// @brief Enqueue one pre-serialised JSON line for async writing.
    ///
    /// Non-blocking; returns immediately. Silently drops the line when the
    /// queue is at capacity. Never throws.
    void append(const std::string& line) noexcept;

    /// @brief Total lines dropped due to queue overflow since construction.
    std::uint64_t drops() const noexcept;

private:
    void worker_loop() noexcept;
    void open_file();
    void rotate_if_needed();

    std::filesystem::path   _path;
    std::uintmax_t          _max_bytes;
    int                     _keep;
    std::size_t             _queue_cap;

    // _file and _bytes_written are owned exclusively by the worker thread.
    std::ofstream           _file;
    std::uintmax_t          _bytes_written = 0;

    mutable std::mutex      _mu;
    std::condition_variable _cv;
    std::deque<std::string> _queue;
    std::uint64_t           _drops = 0;
    bool                    _stop  = false;
    std::thread             _worker;
};

/// @brief Per-IP rate gate for the /feedback endpoint.
///
/// Returns false when the same IP attempts to submit within the TTL window.
/// Expired entries are evicted lazily on each try_consume() call. Not intended
/// for high-QPS use; the /feedback endpoint is low-frequency by design.
class IpCooldown {
public:
    /// @brief Construct with the minimum interval between accepted submissions from the same IP.
    ///
    /// Accepts any std::chrono::duration; nanoseconds covers all finer
    /// durations without truncation.
    explicit IpCooldown(std::chrono::nanoseconds ttl) noexcept;

    /// @brief Return true and record the submission if the IP is not rate-limited.
    ///
    /// Returns false without recording when the same IP last called within the TTL window.
    bool try_consume(const std::string& ip);

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::chrono::nanoseconds                   _ttl;
    std::unordered_map<std::string, TimePoint> _last;
    std::mutex                                 _mu;
};

} // namespace astraea
