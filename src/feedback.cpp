#include "astraea/feedback.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace astraea {

// ---------------------------------------------------------------------------
// JsonlWriter
// ---------------------------------------------------------------------------

JsonlWriter::JsonlWriter(std::filesystem::path path,
                         std::uintmax_t        max_bytes,
                         int                   keep,
                         std::size_t           queue_cap)
    : _path(std::move(path))
    , _max_bytes(max_bytes)
    , _keep(keep > 1 ? keep : 2)
    , _queue_cap(queue_cap > 0 ? queue_cap : 1)
    , _worker([this] { worker_loop(); })
{}

JsonlWriter::~JsonlWriter() {
    {
        std::lock_guard<std::mutex> lk(_mu);
        _stop = true;
    }
    _cv.notify_one();
    _worker.join();
}

void JsonlWriter::append(const std::string& line) noexcept {
    std::lock_guard<std::mutex> lk(_mu);
    if (_queue.size() >= _queue_cap) {
        ++_drops;
        return;
    }
    _queue.push_back(line);
    _cv.notify_one();
}

std::uint64_t JsonlWriter::drops() const noexcept {
    std::lock_guard<std::mutex> lk(_mu);
    return _drops;
}

void JsonlWriter::open_file() {
    std::error_code ec;
    std::filesystem::create_directories(_path.parent_path(), ec);
    if (ec)
        SPDLOG_WARN("JsonlWriter: create_directories failed for {}: {}", _path.string(), ec.message());
    _file.open(_path, std::ios::app | std::ios::binary);
    if (!_file)
        SPDLOG_WARN("JsonlWriter: cannot open {} for append", _path.string());
    // Seed the byte counter from the current file size so rotation thresholds
    // are respected across process restarts.
    const auto sz = std::filesystem::file_size(_path, ec);
    _bytes_written = ec ? 0 : sz;
}

void JsonlWriter::rotate_if_needed() {
    if (_bytes_written <= _max_bytes) return;
    // _bytes_written is maintained exactly by the worker thread, so no
    // file_size() confirmation needed (the stream buffer may not be flushed).
    _file.close(); // flushes buffered data before rename

    std::error_code ec;
    // Shift old rotated files: .(keep-1) -> .keep, ... .1 -> .2
    for (int i = _keep - 1; i >= 1; --i) {
        const auto old_p = _path.parent_path() /
            (_path.stem().string() + "." + std::to_string(i) + _path.extension().string());
        const auto new_p = _path.parent_path() /
            (_path.stem().string() + "." + std::to_string(i + 1) + _path.extension().string());
        if (std::filesystem::exists(old_p, ec))
            std::filesystem::rename(old_p, new_p, ec);
    }

    const auto backup = _path.parent_path() /
        (_path.stem().string() + ".1" + _path.extension().string());
    std::filesystem::rename(_path, backup, ec);
    if (ec)
        SPDLOG_WARN("JsonlWriter: rotate rename failed for {}: {}", _path.string(), ec.message());

    open_file(); // seeds _bytes_written = 0 from the newly created empty file
}

void JsonlWriter::worker_loop() noexcept {
    try {
        open_file();
        while (true) {
            std::deque<std::string> batch;
            {
                std::unique_lock<std::mutex> lk(_mu);
                _cv.wait(lk, [this] { return !_queue.empty() || _stop; });
                batch.swap(_queue);
                if (_stop && batch.empty()) break;
            }
            for (const auto& line : batch) {
                rotate_if_needed();
                if (_file) {
                    _file << line << '\n';
                    _bytes_written += line.size() + 1;
                }
            }
            if (_file) _file.flush();
        }
        // Final drain: process any lines enqueued between the last swap and stop.
        std::deque<std::string> final_batch;
        {
            std::lock_guard<std::mutex> lk(_mu);
            final_batch.swap(_queue);
        }
        for (const auto& line : final_batch) {
            rotate_if_needed();
            if (_file) {
                _file << line << '\n';
                _bytes_written += line.size() + 1;
            }
        }
        if (_file) _file.flush();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("JsonlWriter: worker crashed for {}: {}", _path.string(), e.what());
    } catch (...) {
        SPDLOG_ERROR("JsonlWriter: worker crashed for {} (unknown exception)", _path.string());
    }
}

// ---------------------------------------------------------------------------
// IpCooldown
// ---------------------------------------------------------------------------

IpCooldown::IpCooldown(std::chrono::nanoseconds ttl) noexcept : _ttl(ttl) {}

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
