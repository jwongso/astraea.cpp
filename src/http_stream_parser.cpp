#include "astraea/detail/http_stream_parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <utility>

namespace astraea::detail {

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

inline bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

inline std::string_view trim_lws(std::string_view s) noexcept {
    auto is_lws = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && is_lws(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_lws(s.back()))  s.remove_suffix(1);
    return s;
}

// Pull a single line out of (pending + bytes). On success: line points into
// either _pending (kept alive by caller) or bytes (also caller-owned for the
// duration), bytes is advanced past the line + terminator, returns true.
// Line terminator handling: prefer "\r\n" but accept lone "\n" too.
// On no-line-available: returns false, accumulates into pending.
bool pull_line(std::string& pending, std::string_view& bytes, std::string& line_out) {
    // Fast path: no pending data, look directly in bytes.
    if (pending.empty()) {
        const auto nl = bytes.find('\n');
        if (nl == std::string_view::npos) {
            pending.assign(bytes);
            bytes.remove_prefix(bytes.size());
            return false;
        }
        std::size_t end = nl;
        if (end > 0 && bytes[end - 1] == '\r') --end;
        line_out.assign(bytes.data(), end);
        bytes.remove_prefix(nl + 1);
        return true;
    }
    // Slow path: pending + bytes. Append until we see '\n'.
    const auto nl = bytes.find('\n');
    if (nl == std::string_view::npos) {
        pending.append(bytes.data(), bytes.size());
        bytes.remove_prefix(bytes.size());
        return false;
    }
    pending.append(bytes.data(), nl + 1);
    bytes.remove_prefix(nl + 1);
    // Strip the trailing \n and optional \r from pending.
    std::size_t end = pending.size() - 1; // points at '\n'
    if (end > 0 && pending[end - 1] == '\r') --end;
    line_out.assign(pending.data(), end);
    pending.clear();
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// HttpStreamParser
// ---------------------------------------------------------------------------

void HttpStreamParser::fail(std::string msg) {
    _state = State::Error;
    _error = std::move(msg);
}

bool HttpStreamParser::feed(std::string_view bytes, const BodySink& sink) {
    while (!bytes.empty() && _state != State::Done && _state != State::Error) {
        switch (_state) {
            case State::StatusLine:    if (!consume_status_line(bytes))         return _state != State::Error; break;
            case State::Headers:       if (!consume_headers(bytes))             return _state != State::Error; break;
            case State::BodyLength:    if (!consume_body_length(bytes, sink))   return _state != State::Error; break;
            case State::BodyChunkSize: if (!consume_chunk_size(bytes))          return _state != State::Error; break;
            case State::BodyChunkData: if (!consume_chunk_data(bytes, sink))    return _state != State::Error; break;
            case State::BodyChunkTail: if (!consume_chunk_tail(bytes))          return _state != State::Error; break;
            case State::BodyTrailer:   if (!consume_trailer(bytes))             return _state != State::Error; break;
            case State::Done:
            case State::Error:
                break;
        }
    }
    return _state != State::Error;
}

bool HttpStreamParser::consume_status_line(std::string_view& bytes) {
    std::string line;
    if (!pull_line(_pending, bytes, line)) return false;

    // Parse "HTTP/1.x SP <code> SP <reason>". Reason is optional.
    std::string_view sv(line);
    if (sv.size() < 12 || sv.substr(0, 5) != "HTTP/") {
        fail("invalid status line (no HTTP/ prefix)");
        return false;
    }
    const auto sp1 = sv.find(' ');
    if (sp1 == std::string_view::npos) {
        fail("invalid status line (no SP after version)");
        return false;
    }
    auto rest = sv.substr(sp1 + 1);
    const auto sp2 = rest.find(' ');
    auto code_str = (sp2 == std::string_view::npos) ? rest : rest.substr(0, sp2);
    int code = 0;
    auto [p, ec] = std::from_chars(code_str.data(), code_str.data() + code_str.size(), code);
    if (ec != std::errc{} || p != code_str.data() + code_str.size()) {
        fail("invalid status line (status code not numeric)");
        return false;
    }
    _status_code = code;
    _state = State::Headers;
    return true;
}

bool HttpStreamParser::consume_headers(std::string_view& bytes) {
    while (true) {
        std::string line;
        if (!pull_line(_pending, bytes, line)) return false;
        if (line.empty()) {
            // End of headers. Decide framing.
            if (_chunked) {
                _state = State::BodyChunkSize;
            } else if (_has_content_length) {
                if (_content_remaining == 0) { _state = State::Done; }
                else                         { _state = State::BodyLength; }
            } else {
                // No framing => server closes connection at EOF to delimit.
                // Treat as Content-Length: infinity until close. Use BodyLength
                // path with a sentinel that consume_body_length checks against.
                _content_remaining = static_cast<std::size_t>(-1);
                _state = State::BodyLength;
            }
            return _state != State::Error;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            fail("malformed header line (no colon)");
            return false;
        }
        std::string_view name(line.data(), colon);
        std::string_view value = trim_lws(std::string_view(line).substr(colon + 1));
        if (ieq(name, "transfer-encoding")) {
            // Only "chunked" is handled. Any other token is unsupported.
            if (ieq(value, "chunked")) {
                _chunked = true;
            } else {
                fail("unsupported Transfer-Encoding: " + std::string(value));
                return false;
            }
        } else if (ieq(name, "content-length")) {
            _has_content_length = true;
            std::size_t n = 0;
            auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (ec != std::errc{} || p != value.data() + value.size()) {
                fail("invalid Content-Length value: " + std::string(value));
                return false;
            }
            _content_remaining = n;
        }
        // All other headers are ignored — the LLM endpoint's response shape is fixed.
    }
}

bool HttpStreamParser::consume_body_length(std::string_view& bytes, const BodySink& sink) {
    if (bytes.empty()) return false;
    if (_content_remaining == static_cast<std::size_t>(-1)) {
        // Connection-close framed: deliver everything we got, never advance to Done
        // here — caller signals close via a sentinel feed("", sink) after onClose.
        sink(bytes);
        bytes.remove_prefix(bytes.size());
        return true;
    }
    const auto take = std::min<std::size_t>(_content_remaining, bytes.size());
    sink(bytes.substr(0, take));
    bytes.remove_prefix(take);
    _content_remaining -= take;
    if (_content_remaining == 0) _state = State::Done;
    return true;
}

bool HttpStreamParser::consume_chunk_size(std::string_view& bytes) {
    std::string line;
    if (!pull_line(_pending, bytes, line)) return false;
    // Strip optional chunk extensions after ';'.
    auto semi = line.find(';');
    std::string_view hex = (semi == std::string::npos)
        ? std::string_view(line)
        : std::string_view(line.data(), semi);
    hex = trim_lws(hex);
    if (hex.empty()) { fail("empty chunk size"); return false; }
    std::size_t n = 0;
    auto [p, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), n, 16);
    if (ec != std::errc{} || p != hex.data() + hex.size()) {
        fail("invalid chunk size: " + std::string(hex));
        return false;
    }
    _chunk_remaining = n;
    if (n == 0) {
        // Last chunk. Consume trailer section (zero or more "name: value" lines
        // terminated by an empty line). LLM servers never send trailers in
        // practice, so this is almost always exactly one empty line.
        _state = State::BodyTrailer;
    } else {
        _state = State::BodyChunkData;
    }
    return true;
}

bool HttpStreamParser::consume_chunk_data(std::string_view& bytes, const BodySink& sink) {
    if (bytes.empty()) return false;
    const auto take = std::min<std::size_t>(_chunk_remaining, bytes.size());
    sink(bytes.substr(0, take));
    bytes.remove_prefix(take);
    _chunk_remaining -= take;
    if (_chunk_remaining == 0) _state = State::BodyChunkTail;
    return true;
}

bool HttpStreamParser::consume_chunk_tail(std::string_view& bytes) {
    // CRLF that immediately follows chunk data. Must be empty.
    std::string line;
    if (!pull_line(_pending, bytes, line)) return false;
    if (!line.empty()) {
        fail("expected CRLF after chunk data, got: " + line);
        return false;
    }
    _state = State::BodyChunkSize;
    return true;
}

bool HttpStreamParser::consume_trailer(std::string_view& bytes) {
    // After a 0-sized chunk: read trailer lines until an empty line, then Done.
    std::string line;
    if (!pull_line(_pending, bytes, line)) return false;
    if (line.empty()) {
        _state = State::Done;
    }
    // Non-empty trailer line is silently ignored.
    return true;
}

// ---------------------------------------------------------------------------
// SseLineSplitter
// ---------------------------------------------------------------------------

void SseLineSplitter::feed(std::string_view bytes) {
    for (char c : bytes) {
        if (c == '\r') {
            // Line break. Either \r alone or \r\n - both terminate one line.
            // We'll process now and let the following \n (if any) be ignored
            // when it sees an empty _line_buf and not preceded by a \r.
            process_line(_line_buf);
            _line_buf.clear();
            _expect_lf_ = true;
            continue;
        }
        if (c == '\n') {
            if (_expect_lf_) {
                _expect_lf_ = false;
                continue;  // already processed; skip the \n of "\r\n"
            }
            process_line(_line_buf);
            _line_buf.clear();
            continue;
        }
        _expect_lf_ = false;
        _line_buf.push_back(c);
    }
}

void SseLineSplitter::process_line(std::string_view line) {
    if (line.empty()) {
        flush_event();
        return;
    }
    // RFC 6202 §7: lines starting with ':' are comments, ignored.
    if (line.front() == ':') return;

    const auto colon = line.find(':');
    std::string_view field;
    std::string_view value;
    if (colon == std::string_view::npos) {
        field = line;
        value = {};
    } else {
        field = line.substr(0, colon);
        value = line.substr(colon + 1);
        // Strip a single optional space after the colon (RFC 6202 §7).
        if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    }

    if (field == "data") {
        if (_have_data) _data_buf.push_back('\n');
        _data_buf.append(value.data(), value.size());
        _have_data = true;
    }
    // Other fields (event, id, retry) are ignored - callers that need them
    // can subclass or we can extend the callback set later.
}

void SseLineSplitter::flush_event() {
    if (!_have_data) return;
    _on_data(_data_buf);
    _data_buf.clear();
    _have_data = false;
}

} // namespace astraea::detail
