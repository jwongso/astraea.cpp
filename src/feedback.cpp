#include "astraea/feedback.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

namespace astraea {

// ---------------------------------------------------------------------------
// JsonlWriter
// ---------------------------------------------------------------------------

JsonlWriter::JsonlWriter(std::filesystem::path path,
                         std::uintmax_t        max_bytes,
                         int                   keep)
    : _path(std::move(path))
    , _max_bytes(max_bytes)
    , _keep(keep > 1 ? keep : 2)
{}

void JsonlWriter::rotate_if_needed() {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(_path, ec);
    if (ec || sz <= _max_bytes) return;

    // Shift old rotated files: .(keep-1) -> .keep, ... .1 -> .2
    for (int i = _keep - 1; i >= 1; --i) {
        const auto old_p = _path.parent_path() /
            (_path.stem().string() + "." + std::to_string(i) + _path.extension().string());
        const auto new_p = _path.parent_path() /
            (_path.stem().string() + "." + std::to_string(i + 1) + _path.extension().string());
        if (std::filesystem::exists(old_p, ec))
            std::filesystem::rename(old_p, new_p, ec);
    }

    // Rename live file to .1
    const auto backup = _path.parent_path() /
        (_path.stem().string() + ".1" + _path.extension().string());
    std::filesystem::rename(_path, backup, ec);
    if (ec)
        SPDLOG_WARN("JsonlWriter: rotate rename failed for {}: {}", _path.string(), ec.message());
}

void JsonlWriter::append(const std::string& line) noexcept {
    try {
        std::lock_guard<std::mutex> lk(_mu);
        std::filesystem::create_directories(_path.parent_path());
        rotate_if_needed();
        std::ofstream f(_path, std::ios::app | std::ios::binary);
        if (!f) {
            SPDLOG_WARN("JsonlWriter: cannot open {} for append", _path.string());
            return;
        }
        f << line << '\n';
    } catch (const std::exception& e) {
        SPDLOG_WARN("JsonlWriter: append failed for {}: {}", _path.string(), e.what());
    } catch (...) {
        SPDLOG_WARN("JsonlWriter: append failed for {} (unknown exception)", _path.string());
    }
}

// ---------------------------------------------------------------------------
// IpCooldown
// ---------------------------------------------------------------------------

IpCooldown::IpCooldown(std::chrono::seconds ttl) noexcept : _ttl(ttl) {}

bool IpCooldown::try_consume(const std::string& ip) {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lk(_mu);

    // Evict expired entries lazily to prevent unbounded map growth.
    for (auto it = _last.begin(); it != _last.end(); ) {
        if (now - it->second >= _ttl)
            it = _last.erase(it);
        else
            ++it;
    }

    auto [it, inserted] = _last.emplace(ip, now);
    if (!inserted) {
        if (now - it->second < _ttl) return false;
        it->second = now;
    }
    return true;
}

} // namespace astraea
