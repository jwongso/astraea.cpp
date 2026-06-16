#pragma once
//
// Standalone HTTP/1.1 response parser + Server-Sent Events line splitter.
//
// Pulled out of any network code so they can be unit-tested without trantor /
// drogon / sockets. The Phase 6D streaming LLM client (src/llm_stream.cpp)
// feeds these parsers with the bytes that arrive on the trantor::TcpClient
// recv callback.
//
// What the parser does NOT handle (intentional - LLM endpoint is localhost,
// not the open internet):
//   - HTTPS / TLS (use Drogon's HttpClient for that).
//   - HTTP/2 or HTTP/3.
//   - Compressed bodies (no gzip/deflate decompression).
//   - Trailers beyond the chunked-terminator "0\r\n\r\n".
//   - Persistent connections / keep-alive request pipelining.
//
// What it DOES handle:
//   - Status line: HTTP/1.x SP <code> SP <reason> CRLF
//   - Headers terminated by blank line
//   - Transfer-Encoding: chunked OR Content-Length: <n>
//   - Body bytes delivered as std::string_view to a sink callback as soon as
//     they're available - the entire point of Phase 6D.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace astraea::detail {

/// @brief Sink callback for decoded body bytes as they arrive from the parser.
///
/// Called any number of times during HttpStreamParser::feed(); body bytes are
/// delivered in order. The view is valid only for the duration of the call.
using BodySink = std::function<void(std::string_view)>;

/// @brief Incremental HTTP/1.1 response parser that delivers body bytes to a BodySink.
///
/// Handles both Content-Length and chunked transfer encoding. Pull out of
/// network code so it can be unit-tested without trantor or sockets.
/// Feed as many bytes at a time as the recv callback delivers; no assumption
/// that one call equals one HTTP frame.
class HttpStreamParser {
public:
    /// @brief Parser state machine states.
    enum class State : std::uint8_t {
        StatusLine,    ///< Initial: expecting "HTTP/1.x SP <code> SP <reason> CRLF".
        Headers,       ///< Accumulating header lines until the blank-line terminator.
        BodyLength,    ///< Body framed by Content-Length; _content_remaining bytes remaining.
        BodyChunkSize, ///< Chunked: reading the "<hex>\r\n" size line before each chunk.
        BodyChunkData, ///< Chunked: reading N bytes of chunk content.
        BodyChunkTail, ///< Chunked: CRLF after a data chunk before the next BodyChunkSize.
        BodyTrailer,   ///< Chunked: after 0-size chunk, swallowing trailers until empty line.
        Done,          ///< Body fully received; no further feed() calls needed.
        Error,         ///< Unrecoverable parse failure; error_message() explains.
    };

    /// @brief Feed bytes from the network receive callback.
    ///
    /// Returns false on parse error (state becomes Error and error_message() is
    /// populated). sink is invoked any number of times for body data and never
    /// for status/headers. Safe to call repeatedly with arbitrary chunk sizes.
    bool feed(std::string_view bytes, const BodySink& sink);

    State state() const noexcept { return _state; } ///< Current parser state.
    int status_code() const noexcept { return _status_code; } ///< HTTP status code; 0 until StatusLine is fully parsed.
    const std::string& error_message() const noexcept { return _error; } ///< Human-readable parse error; empty unless state is Error.

private:
    bool consume_status_line(std::string_view& bytes);
    bool consume_headers(std::string_view& bytes);
    bool consume_body_length(std::string_view& bytes, const BodySink& sink);
    bool consume_chunk_size(std::string_view& bytes);
    bool consume_chunk_data(std::string_view& bytes, const BodySink& sink);
    bool consume_chunk_tail(std::string_view& bytes);
    bool consume_trailer(std::string_view& bytes);

    void fail(std::string msg);

    // Accumulator for partial lines that span feed() calls.
    std::string _pending;

    State          _state = State::StatusLine;
    int            _status_code = 0;
    std::size_t    _content_remaining = 0; // for Content-Length framing
    std::size_t    _chunk_remaining   = 0; // for current chunk in chunked framing
    bool           _chunked = false;
    bool           _has_content_length = false;
    std::string    _error;
};

/// @brief Server-Sent Events line splitter that extracts data payloads from raw body bytes.
///
/// Feeds arbitrary-sized raw body chunks in, delivers complete "data: <payload>"
/// event payloads to DataCallback when the event terminates on a blank line.
/// Multiple consecutive data lines are concatenated with "\n" per RFC 6202 S7.
/// "\r\n", "\n", and "\r" all count as line breaks (RFC 6202 S6).
class SseLineSplitter {
public:
    /// @brief Callback invoked with the concatenated data payload of each complete SSE event.
    ///
    /// The view is valid only for the duration of the call. For LLM streaming,
    /// the payload is a JSON object containing choices[0].delta.content.
    using DataCallback = std::function<void(std::string_view)>;

    explicit SseLineSplitter(DataCallback on_data)
        : _on_data(std::move(on_data)) {}

    // Append bytes and dispatch any complete events.
    void feed(std::string_view bytes);

private:
    void process_line(std::string_view line);
    void flush_event();

    DataCallback _on_data;
    std::string  _line_buf;    // accumulating current line
    std::string  _data_buf;    // accumulating "data:" lines until event terminator
    bool         _have_data = false;
    bool         _expect_lf = false; // saw \r, swallow the following \n if present
};

} // namespace astraea::detail
