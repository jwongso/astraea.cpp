#include "astraea/generator.hpp"
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
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

struct GenerateReq {
    std::string model;
    std::vector<ChatMsgJson> messages;
    int max_tokens;
    float temperature;
    bool stream;
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
// SSE parser
//
// Processes a fully-buffered SSE response body (see class NOTE in generator.hpp
// regarding Phase 3 batch delivery). Calls on_token for each non-empty token
// found in choices[0].delta.content.
// ---------------------------------------------------------------------------

std::string parse_sse(std::string_view body, const TokenCallback& on_token) {
    std::string full;
    size_t pos = 0;
    while (pos < body.size()) {
        const auto nl = body.find('\n', pos);
        auto line = (nl == std::string_view::npos)
                  ? body.substr(pos)
                  : body.substr(pos, nl - pos);
        pos = (nl == std::string_view::npos) ? body.size() : nl + 1;

        // Strip trailing \r (SSE allows \r\n line endings).
        if (!line.empty() && line.back() == '\r')
            line = line.substr(0, line.size() - 1);

        if (!line.starts_with("data: ")) continue;
        const auto data = line.substr(6);
        if (data == "[DONE]") break;

        SSEChunk chunk{};
        if (auto pe = glz::read_json(chunk, data); pe) {
            SPDLOG_DEBUG("SSE chunk parse failed: {}", glz::format_error(pe, data));
            continue;
        }
        if (chunk.choices.empty()) continue;

        const auto& token = chunk.choices[0].delta.content;
        if (token.empty()) continue;

        if (on_token) on_token(token);
        full += token;
    }
    return full;
}

drogon::HttpClientPtr make_client(const std::string& url) {
    auto c = drogon::HttpClient::newHttpClient(url);
    if (!c) throw std::invalid_argument("Generator: invalid base_url: " + url);
    return c;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------

Generator::Generator(std::string base_url,
                     std::string model,
                     int max_tokens,
                     float temperature)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _max_tokens(max_tokens)
    , _temperature(temperature)
    , _client(make_client(_base_url))
{}

drogon::Task<std::string> Generator::generate_stream(
    std::vector<ChatMessage> messages,
    TokenCallback on_token) const
{
    using namespace astraea::detail::generator_json;

    std::vector<ChatMsgJson> msgs;
    msgs.reserve(messages.size());
    for (auto& m : messages)
        msgs.push_back({std::move(m.role), std::move(m.content)});

    std::string body;
    if (auto we = glz::write_json(GenerateReq{
            _model, std::move(msgs), _max_tokens, _temperature, /*stream=*/true,
        }, body); we)
        throw std::runtime_error("generate_stream: request serialization failed");

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/v1/chat/completions");
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    // Long LLM timeout: full generation can take 30-120 s for 2500 tokens.
    auto resp = co_await _client->sendRequestCoro(req, /*timeout=*/180.0);
    if (static_cast<int>(resp->statusCode()) != 200)
        throw std::runtime_error("generate_stream: HTTP " +
                                 std::to_string(static_cast<int>(resp->statusCode())));

    co_return parse_sse(resp->body(), on_token);
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
    if (auto pe = glz::read_json(parsed, resp->body()); pe)
        throw std::runtime_error("generate: response parse failed: " +
                                 glz::format_error(pe, resp->body()));
    if (parsed.choices.empty())
        throw std::runtime_error("generate: empty choices in response");

    co_return std::move(parsed.choices[0].message.content);
}

} // namespace astraea
