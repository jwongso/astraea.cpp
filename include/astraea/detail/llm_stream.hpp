#pragma once
//
// LlmStreamSession — single-shot HTTP/1.1 streaming POST client built on
// trantor::TcpClient + the standalone parsers in detail/http_stream_parser.hpp.
//
// Why this exists: Drogon 1.9.7's HttpClient (sendRequest / sendRequestCoro)
// buffers the entire response body before invoking the callback. For SSE-style
// chat-completions endpoints this defeats the purpose of streaming - the
// per-token callback only fires once, in batch, after generation finishes.
// Phase 6D wires a per-token path so /ask/stream can deliver the first byte
// to the client as soon as the LLM produces it.
//
// Scope (matches what the LLM server actually emits on localhost):
//   - HTTP only, no TLS.
//   - Single request per connection; "Connection: close" framing.
//   - Chunked transfer-encoding OR Content-Length.
//   - SSE events extracted via SseLineSplitter (RFC 6202 subset).
//
// Lifetime: must be constructed via std::make_shared and the resulting
// shared_ptr held by the caller for as long as it expects callbacks. The
// session captures itself into the trantor callbacks so it stays alive
// until the connection closes or the LLM emits [DONE]; once finish() runs
// the session releases all callbacks and self-references.
//
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <trantor/net/EventLoop.h>
#include <trantor/net/TcpClient.h>

#include "astraea/detail/http_stream_parser.hpp"

namespace astraea::detail {

class LlmStreamSession
    : public std::enable_shared_from_this<LlmStreamSession>
{
public:
    // Called once per emitted token (already JSON-decoded from the SSE
    // payload). Fires on the trantor event loop the session was constructed
    // with - that's the I/O thread that owns the LLM-side socket.
    using TokenCallback = std::function<void(std::string_view)>;

    // Called exactly once when the stream terminates. nullopt = success
    // (saw [DONE] or natural EOF after Content-Length); std::string = error
    // message describing the failure (HTTP status >= 400, parse failure,
    // connect failure, etc.).
    using DoneCallback = std::function<void(std::optional<std::string>)>;

    // Construct - does NOT start the connection. Call start() to begin.
    // - loop: the trantor event loop the TcpClient and all callbacks run on.
    //         Pass trantor::EventLoop::getEventLoopOfCurrentThread() at the
    //         construction site to preserve loop-affinity for any work that
    //         hops off the on_token callback.
    // - host, port: LLM upstream.
    // - path:  e.g. "/v1/chat/completions".
    // - body:  pre-serialized JSON request body.
    // - on_token, on_done: see above.
    LlmStreamSession(trantor::EventLoop* loop,
                     std::string host, std::uint16_t port,
                     std::string path, std::string body,
                     TokenCallback on_token,
                     DoneCallback  on_done);

    LlmStreamSession(const LlmStreamSession&)            = delete;
    LlmStreamSession& operator=(const LlmStreamSession&) = delete;

    // Kick off the connection. Safe to call from any thread; internally
    // queues the connect onto the captured loop.
    void start();

private:
    void on_connection(const trantor::TcpConnectionPtr& conn);
    void on_message(const trantor::TcpConnectionPtr& conn,
                    trantor::MsgBuffer* buf);
    void handle_sse_event(std::string_view payload);
    void finish(std::optional<std::string> err);

    trantor::EventLoop*               _loop;
    std::string                       _host;
    std::uint16_t                     _port;
    std::string                       _path;
    std::string                       _body;
    TokenCallback                     _on_token;
    DoneCallback                      _on_done;

    std::shared_ptr<trantor::TcpClient> _client;
    HttpStreamParser                    _http;
    std::unique_ptr<SseLineSplitter>    _sse;
    bool                                _finished = false;
};

} // namespace astraea::detail
