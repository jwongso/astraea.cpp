#include "astraea/detail/llm_stream.hpp"
#include "astraea/detail/llm_tcp_pool.hpp"

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include <trantor/net/InetAddress.h>
#include <trantor/net/Resolver.h>
#include <trantor/utils/MsgBuffer.h>

#include <sstream>
#include <utility>

namespace astraea::detail {

// ---------------------------------------------------------------------------
// JSON shape of one SSE event payload from the LLM /v1/chat/completions
// streaming endpoint. Named namespace per the PR #9 glaze-linkage discipline:
// anonymous namespaces give types internal linkage which breaks glaze's
// extern const reflection symbols.
// ---------------------------------------------------------------------------

namespace llm_stream_json {

struct SseDelta  { std::string content; };
struct SseChoice { SseDelta delta; };
struct SseChunk  { std::vector<SseChoice> choices; };

} // namespace llm_stream_json

// ---------------------------------------------------------------------------
// LlmStreamSession
// ---------------------------------------------------------------------------

LlmStreamSession::LlmStreamSession(trantor::EventLoop* loop,
                                   std::string host, std::uint16_t port,
                                   std::string path, std::string body,
                                   TokenCallback on_token,
                                   DoneCallback  on_done,
                                   double timeout_s,
                                   std::shared_ptr<std::atomic<bool>> cancelled,
                                   LlmTcpPool* pool)
    : _loop(loop)
    , _host(std::move(host))
    , _port(port)
    , _path(std::move(path))
    , _body(std::move(body))
    , _on_token(std::move(on_token))
    , _on_done(std::move(on_done))
    , _timeout_s(timeout_s)
    , _cancelled(std::move(cancelled))
    , _pool(pool)
    , _endpoint_key(_host + ":" + std::to_string(_port))
{
    _sse = std::make_unique<SseLineSplitter>(
        [this](std::string_view payload) { handle_sse_event(payload); });
}

void LlmStreamSession::arm_idle_timeout() {
    if (_idle_timer) {
        _loop->invalidateTimer(_idle_timer);
        _idle_timer = 0;
    }
    _idle_timer = _loop->runAfter(_timeout_s, [weak = weak_from_this()]() {
        if (auto self = weak.lock()) {
            self->_idle_timer = 0;
            self->finish("LLM idle timeout: no data for " +
                std::to_string(static_cast<int>(self->_timeout_s)) + "s");
        }
    });
}

void LlmStreamSession::start() {
    auto self = shared_from_this();
    _loop->runInLoop([self]() {
        if (self->_timeout_s > 0.0) self->arm_idle_timeout();

        // Pool fast path: try to reuse an idle connection from a prior
        // request on this loop. The pool returns nullptr if empty or if
        // all idle entries proved disconnected; in either case we fall
        // through to the connect-from-scratch path below.
        if (self->_pool) {
            if (auto pooled = self->_pool->try_acquire(self->_loop, self->_endpoint_key)) {
                self->_client = std::move(pooled);
                // Rebind callbacks - the no-op handlers installed by the
                // previous user's dispose_client() would otherwise swallow
                // every incoming byte.
                self->_client->setConnectionCallback(
                    [self](const trantor::TcpConnectionPtr& conn) {
                        self->on_connection(conn);
                    });
                self->_client->setMessageCallback(
                    [self](const trantor::TcpConnectionPtr& conn,
                           trantor::MsgBuffer* buf) {
                        self->on_message(conn, buf);
                    });
                self->_client->setConnectionErrorCallback(
                    [self]() {
                        self->finish("LLM connection error on pooled connection");
                    });
                // Connection is already up - skip resolver + connect, send
                // the request straight away by simulating an on_connection.
                auto conn = self->_client->connection();
                if (conn && conn->connected()) {
                    self->on_connection(conn);
                    return;
                }
                // Race: connection died between pool acquire and our send.
                // Drop the dead client and fall through to fresh-connect.
                self->_client.reset();
            }
        }

        // Resolve hostname asynchronously via trantor's Resolver. For
        // an IPv4 literal this completes synchronously inside the callback;
        // for a real hostname it goes through c-ares (or POSIX getaddrinfo
        // depending on the trantor build) without blocking the event loop.
        auto resolver = trantor::Resolver::newResolver(self->_loop);
        // Keep the resolver alive for the duration of the lookup by capturing
        // it into the resolve callback.
        resolver->resolve(self->_host,
            [self, resolver](const trantor::InetAddress& addr) {
                if (addr.ipNetEndian() == 0) {
                    self->finish("LLM DNS resolution failed for host: " + self->_host);
                    return;
                }
                trantor::InetAddress resolved(addr.toIp(), self->_port);
                self->_client = std::make_shared<trantor::TcpClient>(
                    self->_loop, resolved, "astraea-llm-stream");

                self->_client->setConnectionCallback(
                    [self](const trantor::TcpConnectionPtr& conn) {
                        self->on_connection(conn);
                    });
                self->_client->setMessageCallback(
                    [self](const trantor::TcpConnectionPtr& conn,
                           trantor::MsgBuffer* buf) {
                        self->on_message(conn, buf);
                    });
                self->_client->setConnectionErrorCallback(
                    [self]() {
                        self->finish("LLM connection failed (refused / DNS / unreachable)");
                    });
                // Single-shot: no retry on close, no automatic reconnect.
                // Retrying mid-generation would replay the entire prompt
                // against the LLM and blow token budget.
                self->_client->connect();
            });
    });
}

void LlmStreamSession::on_connection(const trantor::TcpConnectionPtr& conn) {
    if (!conn) return;
    if (conn->connected()) {
        // Send the HTTP request immediately. trantor handles partial writes
        // via its outbound buffer; conn->send() returns after queueing.
        //
        // Connection: keep-alive lets the LlmTcpPool reuse this socket
        // across sequential requests on the same loop. The server must
        // also frame the body so we can detect end-of-response (typically
        // chunked transfer-encoding for SSE); close-framed responses
        // can never be pooled and fall back to drop-on-finish.
        std::ostringstream req;
        req << "POST " << _path << " HTTP/1.1\r\n"
            << "Host: " << _host << ":" << _port << "\r\n"
            << "User-Agent: astraea/0.1\r\n"
            << "Accept: text/event-stream\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << _body.size() << "\r\n"
            << "Connection: keep-alive\r\n"
            << "\r\n"
            << _body;
        conn->send(req.str());
    } else {
        // Disconnected. If we haven't already finished (success path via
        // [DONE] sentinel), treat the close as end-of-stream. The HTTP
        // parser will be in Done state on graceful completion; if not,
        // it's a server-side abort.
        if (_http.state() == HttpStreamParser::State::Done) {
            finish(std::nullopt);
        } else if (_http.state() == HttpStreamParser::State::Error) {
            finish("LLM response parse error: " + _http.error_message());
        } else if (_finished && _pool_on_done && !_client_disposed) {
            // Pool-deferred path: server closed before we could see the
            // chunked terminator. We can't pool a disconnected client,
            // so drop it now to break the reference cycle - otherwise the
            // session leaks (callbacks still bound, _client still held).
            dispose_client(/*to_pool=*/false);
        } else {
            finish("LLM connection closed mid-stream "
                   "(http_state=" + std::to_string(static_cast<int>(_http.state())) +
                   ", http_status=" + std::to_string(_http.status_code()) + ")");
        }
    }
}

void LlmStreamSession::on_message(const trantor::TcpConnectionPtr& /*conn*/,
                                  trantor::MsgBuffer* buf) {
    // If we've already torn down the underlying TcpClient (disposed - either
    // returned to the pool or disconnected), accept no further bytes.
    // Otherwise: even after finish() has signalled the awaiter, we keep
    // feeding the parser so the chunked terminator that follows [DONE]
    // can land and trigger the deferred pool-release path.
    if (_client_disposed) {
        buf->retrieveAll();
        return;
    }
    // Data arrived: reset the idle timer so a slow but still-active generation
    // (producing tokens steadily within the window) is not aborted.
    if (_timeout_s > 0.0) arm_idle_timeout();
    // Take everything trantor has buffered and feed it to the parser. The
    // parser's body-sink hands decoded body bytes to the SSE splitter which
    // in turn fires handle_sse_event for each complete event.
    std::string_view view(buf->peek(), buf->readableBytes());
    const std::size_t to_consume = view.size();
    const bool ok = _http.feed(view, [this](std::string_view body) {
        _sse->feed(body);
    });
    buf->retrieve(to_consume);

    if (!ok) {
        finish("LLM HTTP parse error: " + _http.error_message());
        return;
    }
    if (_http.state() == HttpStreamParser::State::Done) {
        // End of the HTTP body. Two cases:
        //   1. We already saw [DONE] and finish(nullopt) ran with the pool
        //      path. _pool_on_done is set. Dispose to pool now - this is
        //      the only safe moment because the trailing chunked terminator
        //      has just been consumed and there are no more bytes in flight.
        //   2. Body ended without [DONE] sentinel. Could be a non-stream
        //      response (shouldn't happen on /chat/completions stream=true)
        //      or a server bug. finish() with nullopt; drop the client
        //      since we can't trust the framing for reuse.
        if (_pool_on_done) {
            dispose_client(/*to_pool=*/true);
        } else if (!_finished) {
            finish(std::nullopt);
            dispose_client(/*to_pool=*/false);
        }
        // else: !_pool_on_done && _finished => error path already disposed.
    } else if (_http.status_code() != 0 && _http.status_code() >= 400 &&
               _http.state() != HttpStreamParser::State::Headers &&
               _http.state() != HttpStreamParser::State::StatusLine) {
        // Server returned an error status. Don't wait for the connection to
        // close - surface the failure immediately.
        finish("LLM returned HTTP " + std::to_string(_http.status_code()));
    }
}

void LlmStreamSession::handle_sse_event(std::string_view payload) {
    if (_finished) return;
    if (payload == "[DONE]") {
        finish(std::nullopt);
        return;
    }
    using namespace llm_stream_json;
    SseChunk chunk;
    if (auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(chunk, payload); err) {
        // Malformed JSON in an SSE chunk: log and skip (matches the Python
        // generator which silently swallows JSONDecodeError in this loop).
        SPDLOG_DEBUG("LLM SSE chunk parse failed, skipping");
        return;
    }
    if (chunk.choices.empty()) return;
    const auto& tok = chunk.choices.front().delta.content;
    if (tok.empty()) return;
    _on_token(tok);
    // If the downstream client disconnected (peer_dead set by the token
    // callback's failed send), abort the upstream LLM connection now rather
    // than waiting for [DONE]. This releases the global LLM semaphore
    // immediately instead of holding it for the remainder of generation.
    // Mirrors the [DONE] path: same finish(nullopt) call, same control flow.
    if (_cancelled && _cancelled->load(std::memory_order_relaxed))
        finish(std::nullopt);
}

void LlmStreamSession::finish(std::optional<std::string> err) {
    if (_finished) return;
    _finished = true;
    if (_idle_timer) {
        _loop->invalidateTimer(_idle_timer);
        _idle_timer = 0;
    }
    // Move callbacks out before invoking so a re-entrant call into the
    // session (e.g. an awaiter that resumes synchronously and tries to
    // start a new request on the same session) sees the finished state.
    auto cb = std::move(_on_done);
    _on_token = nullptr;

    // Decide whether to dispose the TcpClient now (error path) or defer
    // it to on_message after the chunked terminator arrives (pool path).
    //
    // The pool path requires three conditions:
    //   1. !err  (semantically a successful response; not a parse error
    //             or peer disconnect)
    //   2. _pool != null  (caller opted into pooling)
    //   3. parser will reach Done state after we return  (i.e. the server
    //      framed the body so we can know when it ends)
    //
    // Condition #3 is the subtle one. For chunked-encoded responses the
    // parser will see the "0\r\n\r\n" terminator shortly and reach Done.
    // For Content-Length-framed responses it's already Done if we hit
    // [DONE] at the same byte offset as the body end - unusual but
    // possible. For close-framed bodies (no chunked, no Content-Length;
    // typical of older SSE servers), the parser stays in BodyLength
    // forever waiting for FIN - condition #3 never holds and we drop.
    //
    // We can't reliably distinguish #3 here without inspecting future
    // bytes, so we set _pool_on_done=true and let on_message be the
    // arbiter: if it sees Done within the existing idle-timeout window
    // (default 300s) it pools; otherwise the eventual idle-timeout
    // fires another finish() with err and we drop.
    const bool can_pool = !err && _pool != nullptr && _client;
    if (can_pool) {
        _pool_on_done = true;
        // Don't dispose yet. on_message will call dispose_client(true)
        // when the parser reaches Done state.
        //
        // Safety net: if the chunked terminator never arrives (server is
        // misbehaving, body framing is close-only and server never closes,
        // etc.) we'd leak the session (callbacks still hold shared_ptr<this>).
        // Schedule a short fallback drop. 2s is well over the typical
        // [DONE]-to-terminator gap (sub-millisecond on localhost) but well
        // under the 300s idle timeout, so it dominates only the misbehaving
        // path. weak_from_this so the timer never extends our lifetime.
        _loop->runAfter(2.0, [weak = weak_from_this()]() {
            if (auto self = weak.lock()) {
                if (!self->_client_disposed) {
                    self->dispose_client(/*to_pool=*/false);
                }
            }
        });
    } else {
        // Error path or no pool: dispose immediately.
        dispose_client(/*to_pool=*/false);
    }

    if (cb) cb(std::move(err));
}

void LlmStreamSession::dispose_client(bool to_pool) {
    if (_client_disposed || !_client) {
        _client_disposed = true;
        _client.reset();
        return;
    }
    // Break the reference cycle: the trantor callbacks captured shared_ptr<this>,
    // and `this` owns `_client`. Without clearing the callbacks, the session
    // can only be released when trantor finally destroys the TcpClient (which
    // it won't, because the closures keep `this` alive which keeps `_client`
    // alive). Empty lambdas drop the captured shared_ptr<this>.
    _client->setMessageCallback([](const trantor::TcpConnectionPtr&,
                                   trantor::MsgBuffer*) {});
    _client->setConnectionCallback([](const trantor::TcpConnectionPtr&) {});
    _client->setConnectionErrorCallback([]() {});
    if (to_pool && _pool) {
        // Hand back to the pool for the next session to reuse. The pool's
        // release() validates that the connection is still up and silently
        // drops dead entries, so a server-side close racing with our
        // release here causes no functional issue.
        _pool->release(_loop, _endpoint_key, std::move(_client));
    } else {
        _client->disconnect();
        _client.reset();
    }
    _client_disposed = true;
}

} // namespace astraea::detail
