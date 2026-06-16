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
//   - Single request per pooled connection (HTTP/1.1 keep-alive enabled
//     via the LlmTcpPool; on connections served by chunked-encoded SSE
//     the underlying TcpClient is released to the pool after the chunked
//     terminator; on non-chunked SSE responses framed by TCP-close the
//     pool gets no entries and behaviour reduces to the pre-pool path).
//   - Chunked transfer-encoding OR Content-Length.
//   - SSE events extracted via SseLineSplitter (RFC 6202 subset).
//
// Lifetime: must be constructed via std::make_shared and the resulting
// shared_ptr held by the caller for as long as it expects callbacks. The
// session captures itself into the trantor callbacks so it stays alive
// until the connection closes or the LLM emits [DONE]; once finish() runs
// the session releases all callbacks and self-references.
//
// Cancellation: pass a non-null `cancelled` flag to the constructor. After
// each on_token call, the session checks the flag; if set, it calls finish()
// immediately, closing the TCP connection to the LLM and firing on_done so
// the semaphore is released without waiting for [DONE].
//
// Pooling: an optional LlmTcpPool* lets sequential requests on the same
// event loop reuse the underlying trantor::TcpClient. The session pulls
// an idle connection from the pool at start() (falling back to a fresh
// one if the pool is empty) and returns it on success-with-Done-state.
// On any error path - parser fail, peer disconnect, idle timeout, [DONE]
// without a subsequent chunked terminator - the connection is dropped
// instead. Passing nullptr disables pooling entirely; same behaviour as
// pre-pool code paths.
//
#include <atomic>
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

/// @brief Single-shot HTTP/1.1 streaming POST client for the LLM /v1/chat/completions endpoint.
///
/// Built on trantor::TcpClient with the standalone parsers in
/// detail/http_stream_parser.hpp. Bypasses Drogon's buffering HttpClient so
/// TokenCallback fires per token as the LLM emits it (true per-token streaming,
/// Phase 6D). Must be constructed via std::make_shared; captures itself into
/// trantor callbacks to stay alive until finish() runs.
///
/// See the file-level comment for pooling, cancellation, and lifetime details.
class LlmStreamSession
    : public std::enable_shared_from_this<LlmStreamSession>
{
public:
    /// @brief Called once per decoded token from the SSE stream.
    ///
    /// Fires on the trantor event loop the session was constructed with.
    /// The string_view is valid only for the duration of the call.
    using TokenCallback = std::function<void(std::string_view)>;

    /// @brief Called exactly once when the stream terminates.
    ///
    /// nullopt indicates success (saw [DONE] or natural EOF after Content-Length).
    /// A non-empty string contains an error description (HTTP status >= 400,
    /// parse failure, connect failure, idle timeout, etc.).
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
    // timeout_s: idle timeout in seconds. If no data is received from the LLM
    // for this many seconds after start() is called, the session is aborted with
    // an error. This covers both the connect phase (server never responds) and
    // mid-stream stalls (server stops sending tokens). 0 disables the timeout.
    // The timer resets on every on_message() call, so it is strictly an idle
    // (no-data) timeout rather than an overall wall-clock deadline.
    LlmStreamSession(trantor::EventLoop* loop,
                     std::string host, std::uint16_t port,
                     std::string path, std::string body,
                     TokenCallback on_token,
                     DoneCallback  on_done,
                     double timeout_s = 0.0,
                     std::shared_ptr<std::atomic<bool>> cancelled = nullptr,
                     class LlmTcpPool* pool = nullptr);

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
    // Release the underlying TcpClient: pool it (when safe) or disconnect.
    // Called exactly once per session lifetime; idempotent on a moved-from
    // _client. The `to_pool` decision is computed by the caller based on
    // success state + parser Done state.
    void dispose_client(bool to_pool);

    // Arm (or re-arm) the idle timer. Must be called on the event loop thread.
    // Cancels the existing timer (if any) before scheduling a new one.
    void arm_idle_timeout();

    trantor::EventLoop*               _loop;
    std::string                       _host;
    std::uint16_t                     _port;
    std::string                       _path;
    std::string                       _body;
    TokenCallback                     _on_token;
    DoneCallback                      _on_done;
    double                            _timeout_s;
    std::shared_ptr<std::atomic<bool>> _cancelled;
    class LlmTcpPool*                 _pool       = nullptr;
    std::string                       _endpoint_key;     // "host:port"
    trantor::TimerId                  _idle_timer = 0;

    std::shared_ptr<trantor::TcpClient> _client;
    HttpStreamParser                    _http;
    std::unique_ptr<SseLineSplitter>    _sse;
    bool                                _finished        = false;
    // Deferred-disposal state. finish() doesn't tear down the TcpClient
    // immediately on the success path - the chunked terminator may still
    // be in flight after the [DONE] SSE sentinel. on_message watches for
    // parser Done state and disposes when it arrives. _client_disposed
    // guards against double-dispose if on_message and an error path race.
    bool                                _pool_on_done    = false;
    bool                                _client_disposed = false;
};

} // namespace astraea::detail
