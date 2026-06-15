#include "astraea/generator.hpp"
#include "astraea/detail/llm_stream.hpp"
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>
#include <trantor/net/EventLoop.h>
#include <coroutine>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

// JSON structs must have external linkage for glaze reflection.
// Anonymous-namespace types are TU-local ([basic.link]) and break
// glz::detail::external<T> on Clang 18 + libc++.
// Function-local types also have no linkage, so NonStreamMsg/Choice/Resp
// live here rather than inside generate().
namespace astraea::detail::generator_json {

// ---------------------------------------------------------------------------
// Request structs
// ---------------------------------------------------------------------------

struct ChatMsgJson {
    std::string role;
    std::string content;
};

struct ChatTemplateKwargs {
    bool enable_thinking;
};

struct GenerateReq {
    std::string model;
    std::vector<ChatMsgJson> messages;
    int max_tokens;
    float temperature;
    bool stream;
    ChatTemplateKwargs chat_template_kwargs;
};

// ---------------------------------------------------------------------------
// SSE response structs (one chunk per "data: {...}" line)
// ---------------------------------------------------------------------------

struct SSEDelta  { std::string content; };
struct SSEChoice { SSEDelta delta; };
struct SSEChunk  { std::vector<SSEChoice> choices; };

// ---------------------------------------------------------------------------
// Non-streaming response structs
// ---------------------------------------------------------------------------

struct NonStreamMsg    { std::string content; };
struct NonStreamChoice { NonStreamMsg message; };
struct NonStreamResp   { std::vector<NonStreamChoice> choices; };

} // namespace astraea::detail::generator_json

namespace astraea {

namespace {

using namespace astraea::detail::generator_json;

// ---------------------------------------------------------------------------
// Minimal http:// URL parser. The LLM endpoint is always plain HTTP on
// localhost so we hard-reject https:// and anything exotic. Returns
// (host, port, base_path) where base_path is everything from the leading
// '/' onward (possibly empty - we treat empty as "/").
// ---------------------------------------------------------------------------

struct ParsedUrl {
    std::string   host;
    std::uint16_t port;
    std::string   base_path;
};

ParsedUrl parse_http_url(const std::string& url) {
    constexpr std::string_view scheme = "http://";
    if (url.rfind(scheme, 0) != 0)
        throw std::invalid_argument("Generator: only http:// LLM URLs supported, got: " + url);
    auto rest = std::string_view(url).substr(scheme.size());

    const auto slash = rest.find('/');
    auto authority = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    std::string base_path = (slash == std::string_view::npos)
        ? std::string{}
        : std::string(rest.substr(slash));

    std::uint16_t port = 80;
    std::string host;
    const auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        host.assign(authority);
    } else {
        host.assign(authority.substr(0, colon));
        auto port_str = authority.substr(colon + 1);
        int p = 0;
        for (char c : port_str) {
            if (c < '0' || c > '9')
                throw std::invalid_argument("Generator: bad port in URL: " + url);
            p = p * 10 + (c - '0');
            if (p > 65535)
                throw std::invalid_argument("Generator: port out of range in URL: " + url);
        }
        port = static_cast<std::uint16_t>(p);
    }
    if (host.empty())
        throw std::invalid_argument("Generator: empty host in URL: " + url);
    return {std::move(host), port, std::move(base_path)};
}

drogon::HttpClientPtr make_client(const std::string& url) {
    auto c = drogon::HttpClient::newHttpClient(url);
    if (!c) throw std::invalid_argument("Generator: invalid base_url: " + url);
    return c;
}

// ---------------------------------------------------------------------------
// Stream awaiter — bridges astraea::detail::LlmStreamSession (callback-based)
// into the coroutine that owns generate_stream(). Race-safe under the
// await_ready/await_suspend boundary: both check `finished` under the same
// mutex so a finish() landing between the two cannot drop the resume.
// ---------------------------------------------------------------------------

struct StreamState {
    std::mutex                  mu;
    std::coroutine_handle<>     continuation;
    bool                        finished = false;
    std::optional<std::string>  err;
};

struct StreamAwaiter {
    std::shared_ptr<StreamState> st;

    bool await_ready() noexcept {
        std::lock_guard<std::mutex> lk(st->mu);
        return st->finished;
    }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->finished) return false;  // race: finish() already landed
        st->continuation = h;
        return true;
    }
    void await_resume() {
        if (st->err) throw std::runtime_error(*st->err);
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

Generator::Generator(std::string base_url,
                     std::string model,
                     int max_tokens,
                     float temperature,
                     bool enable_thinking,
                     double stream_idle_timeout_s)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _max_tokens(max_tokens)
    , _temperature(temperature)
    , _enable_thinking(enable_thinking)
    , _stream_idle_timeout_s(stream_idle_timeout_s)
    , _client(make_client(_base_url))
{}

drogon::Task<std::string> Generator::generate_stream(
    std::vector<ChatMessage> messages,
    TokenCallback on_token,
    std::shared_ptr<std::atomic<bool>> cancelled) const
{
    using namespace astraea::detail::generator_json;

    std::vector<ChatMsgJson> msgs;
    msgs.reserve(messages.size());
    for (auto& m : messages)
        msgs.push_back({std::move(m.role), std::move(m.content)});

    std::string body;
    if (auto we = glz::write_json(GenerateReq{
            _model, std::move(msgs), _max_tokens, _temperature, /*stream=*/true,
            ChatTemplateKwargs{_enable_thinking},
        }, body); we)
        throw std::runtime_error("generate_stream: request serialization failed");

    // Phase 6D path: bypass Drogon's HttpClient (which buffers the full
    // response before returning) and use astraea::detail::LlmStreamSession
    // built on raw trantor::TcpClient. Tokens fire on_token as soon as the
    // LLM emits each SSE chunk - true per-token streaming end-to-end.
    auto       url    = parse_http_url(_base_url);
    const auto path   = url.base_path.empty()
        ? std::string{"/chat/completions"}
        : url.base_path + "/chat/completions";

    auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
    if (!loop)
        throw std::runtime_error("generate_stream: must be called from a trantor event loop");

    auto state = std::make_shared<StreamState>();
    std::string accumulator;

    // Capture &accumulator + on_token by reference - both live in this
    // coroutine frame and outlive the session (the session calls on_done
    // before going away, and we don't co_return until on_done resumes us).
    auto session = std::make_shared<astraea::detail::LlmStreamSession>(
        loop, std::move(url.host), url.port, path, std::move(body),
        /*on_token=*/[&accumulator, &on_token](std::string_view tok) {
            accumulator.append(tok);
            if (on_token) on_token(tok);
        },
        /*on_done=*/[state, loop](std::optional<std::string> err) {
            std::coroutine_handle<> h;
            {
                std::lock_guard<std::mutex> lk(state->mu);
                state->finished = true;
                state->err      = std::move(err);
                h               = state->continuation;
                state->continuation = {};
            }
            if (!h) return;
            // Resume on the original loop. on_done fires on the I/O thread
            // (same loop the session was constructed with), so this is
            // usually a direct resume. Hop only if for some reason it isn't.
            if (loop == trantor::EventLoop::getEventLoopOfCurrentThread()) {
                h.resume();
            } else {
                loop->queueInLoop([h]() { h.resume(); });
            }
        },
        /*timeout_s=*/_stream_idle_timeout_s,
        /*cancelled=*/std::move(cancelled));
    session->start();

    co_await StreamAwaiter{state};
    co_return std::move(accumulator);
}

drogon::Task<std::string> Generator::generate(
    std::vector<ChatMessage> messages) const
{
    using namespace astraea::detail::generator_json;

    std::vector<ChatMsgJson> msgs;
    msgs.reserve(messages.size());
    for (auto& m : messages)
        msgs.push_back({std::move(m.role), std::move(m.content)});

    std::string body;
    if (auto we = glz::write_json(GenerateReq{
            _model, std::move(msgs), _max_tokens, _temperature, /*stream=*/false,
            ChatTemplateKwargs{_enable_thinking},
        }, body); we)
        throw std::runtime_error("generate: request serialization failed");

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/v1/chat/completions");
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto resp = co_await _client->sendRequestCoro(req, /*timeout=*/60.0);
    if (static_cast<int>(resp->statusCode()) != 200)
        throw std::runtime_error("generate: HTTP " +
                                 std::to_string(static_cast<int>(resp->statusCode())));

    // Non-streaming response: {"choices":[{"message":{"content":"..."}}]}
    NonStreamResp parsed{};
    if (auto pe = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, resp->body()); pe)
        throw std::runtime_error("generate: response parse failed: " +
                                 glz::format_error(pe, resp->body()));
    if (parsed.choices.empty())
        throw std::runtime_error("generate: empty choices in response");

    co_return std::move(parsed.choices[0].message.content);
}

} // namespace astraea
