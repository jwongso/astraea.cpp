#include "astraea/detail/llm_stream.hpp"

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
                                   std::shared_ptr<std::atomic<bool>> cancelled)
    : _loop(loop)
    , _host(std::move(host))
    , _port(port)
    , _path(std::move(path))
    , _body(std::move(body))
    , _on_token(std::move(on_token))
    , _on_done(std::move(on_done))
    , _timeout_s(timeout_s)
    , _cancelled(std::move(cancelled))
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
        std::ostringstream req;
        req << "POST " << _path << " HTTP/1.1\r\n"
            << "Host: " << _host << ":" << _port << "\r\n"
            << "User-Agent: astraea/0.1\r\n"
            << "Accept: text/event-stream\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << _body.size() << "\r\n"
            << "Connection: close\r\n"
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
        } else {
            finish("LLM connection closed mid-stream "
                   "(http_state=" + std::to_string(static_cast<int>(_http.state())) +
                   ", http_status=" + std::to_string(_http.status_code()) + ")");
        }
    }
}

void LlmStreamSession::on_message(const trantor::TcpConnectionPtr& /*conn*/,
                                  trantor::MsgBuffer* buf) {
    if (_finished) {
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
        // Server may close gracefully now; on_connection's disconnect branch
        // will fire and call finish(nullopt). We don't preemptively close
        // here because some servers send a final chunk after the body.
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
    if (_client) {
        // Break the reference cycle: the trantor callbacks captured shared_ptr<this>,
        // and `this` owns `_client`. Without clearing the callbacks, the session
        // can only be released when trantor finally destroys the TcpClient (which
        // it won't, because the closures keep `this` alive which keeps `_client`
        // alive). Empty lambdas drop the captured shared_ptr<this>.
        _client->setMessageCallback([](const trantor::TcpConnectionPtr&,
                                       trantor::MsgBuffer*) {});
        _client->setConnectionCallback([](const trantor::TcpConnectionPtr&) {});
        _client->setConnectionErrorCallback([]() {});
        _client->disconnect();
    }
    if (cb) cb(std::move(err));
}

} // namespace astraea::detail
