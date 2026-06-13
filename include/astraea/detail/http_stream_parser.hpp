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

// Sink for raw decoded body bytes. Called any number of times during parse()
// with chunks of body data as they're consumed. Body bytes are delivered in
// order; the caller is responsible for any further demuxing (e.g. SSE line
// splitting). The view is valid only for the duration of the call.
using BodySink = std::function<void(std::string_view)>;

class HttpStreamParser {
public:
    enum class State : std::uint8_t {
        StatusLine,    // initial: expecting "HTTP/1.x SP <code> SP <reason> CRLF"
        Headers,       // accumulating header lines until blank-line terminator
        BodyLength,    // body framed by Content-Length, _content_remaining set
        BodyChunkSize, // chunked: reading "<hex>\r\n" before each chunk
        BodyChunkData, // chunked: reading <n> bytes of chunk content
        BodyChunkTail, // chunked: CRLF after data chunk -> back to BodyChunkSize
        BodyTrailer,   // chunked: after 0-size chunk, swallow trailers until empty line -> Done
        Done,          // body fully received
        Error,         // unrecoverable parse failure - error_message() explains
    };

    // Feed bytes from the wire. Returns false on parse error (state becomes
    // Error and error_message() is populated). `sink` is called any number
    // of times for body data; never called for status/headers.
    //
    // Safe to call repeatedly with whatever the recv callback hands you - no
    // assumption that one call == one HTTP frame.
    bool feed(std::string_view bytes, const BodySink& sink);

    State state() const noexcept { return _state; }
    int status_code() const noexcept { return _status_code; }
    const std::string& error_message() const noexcept { return _error; }

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

// SSE line splitter. Feeds raw decoded body bytes in (any size), pulls out
// "data: <payload>\n\n" events and hands the <payload> portion to the
// callback. Other SSE fields (event:, id:, retry:) are surfaced via
// on_field for callers that need event-type dispatch; ignore in callers
// that only care about data.
//
// The splitter buffers partial lines internally so callers can feed
// arbitrary-sized chunks. "\r\n", "\n", and "\r" all count as line breaks
// (RFC 6202 §6).
class SseLineSplitter {
public:
    // Called with the trimmed value following "data: " (or "data:") on each
    // complete data line. Multiple consecutive data lines are concatenated
    // with "\n" per RFC 6202 §7, then delivered when the event terminates
    // (blank line).
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
    bool         _expect_lf_ = false; // saw \r, swallow the following \n if present
};

} // namespace astraea::detail
