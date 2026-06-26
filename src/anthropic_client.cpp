#include "astraea/anthropic_client.hpp"
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <format>
#include <stdexcept>

// ---------------------------------------------------------------------------
// JSON structs (Anthropic Messages API)
// Must have external linkage for glaze reflection - named namespace required.
// ---------------------------------------------------------------------------
namespace astraea::detail::anthropic_json {

struct MsgJson {
    std::string role;
    std::string content;
};

struct ReqJson {
    std::string           model;
    int                   max_tokens{};
    std::string           system;
    std::vector<MsgJson>  messages;
};

struct ContentBlock {
    std::string type;
    std::string text;
};

struct RespJson {
    std::string                type;
    std::vector<ContentBlock>  content;
};

struct ErrorDetail { std::string type; std::string message; };
struct ErrorResp   { std::string type; ErrorDetail error; };

} // namespace astraea::detail::anthropic_json

namespace astraea {

using namespace astraea::detail::anthropic_json;

// ---------------------------------------------------------------------------
// AnthropicClient
// ---------------------------------------------------------------------------

AnthropicClient::AnthropicClient(std::string api_key,
                                 std::string model,
                                 int         max_tok,
                                 double      timeout)
    : _api_key(std::move(api_key))
    , _model(std::move(model))
    , _max_tok(max_tok)
    , _timeout(timeout)
{
    _client = drogon::HttpClient::newHttpClient("https://api.anthropic.com");
    if (!_client)
        throw std::runtime_error("AnthropicClient: failed to create HTTPS client");
}

drogon::Task<std::string>
AnthropicClient::complete(std::string_view system_prompt,
                          std::string_view user_prompt)
{
    co_return co_await complete(system_prompt,
        std::vector<AnthropicMessage>{{"user", std::string(user_prompt)}});
}

drogon::Task<std::string>
AnthropicClient::complete(std::string_view              system_prompt,
                          std::vector<AnthropicMessage> messages)
{
    ReqJson req;
    req.model      = _model;
    req.max_tokens = _max_tok;
    req.system     = std::string(system_prompt);
    req.messages.reserve(messages.size());
    for (auto& m : messages)
        req.messages.push_back({m.role, m.content});

    std::string body;
    if (auto e = glz::write_json(req, body); e)
        throw std::runtime_error("AnthropicClient: request serialization failed");

    // Per-call telemetry. Includes request size, system+user lengths, model,
    // status code, response size, and end-to-end latency. Logged at info level
    // so it shows up in the console without enabling debug.
    const std::size_t sys_chars  = system_prompt.size();
    std::size_t user_chars = 0;
    for (auto& m : messages) user_chars += m.content.size();
    spdlog::info("[anthropic] -> POST /v1/messages model={} sys={}c user={}c "
                 "body={}c max_tokens={} timeout={}s",
                 _model, sys_chars, user_chars, body.size(), _max_tok, _timeout);

    auto http_req = drogon::HttpRequest::newHttpRequest();
    http_req->setMethod(drogon::Post);
    http_req->setPath("/v1/messages");
    http_req->addHeader("x-api-key",          _api_key);
    http_req->addHeader("anthropic-version",  "2023-06-01");
    http_req->addHeader("content-type",       "application/json");
    http_req->setBody(body);

    const auto t0 = std::chrono::steady_clock::now();
    drogon::HttpResponsePtr resp;
    try {
        resp = co_await _client->sendRequestCoro(http_req, _timeout);
    } catch (const std::exception& e) {
        const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        spdlog::error("[anthropic] <- network error after {}ms: {} "
                      "(timeout was {}s)", dt, e.what(), _timeout);
        throw;
    }
    const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0).count();

    const int status = static_cast<int>(resp->statusCode());
    const auto body_size = resp->body().size();
    spdlog::info("[anthropic] <- HTTP {} in {}ms resp={}c",
                 status, dt_ms, body_size);

    if (status != 200) {
        ErrorResp err{};
        (void)glz::read<glz::opts{.error_on_unknown_keys = false}>(err, resp->body());
        const std::string msg = err.error.message.empty()
                                ? std::string(resp->body().substr(0, 200))
                                : err.error.message;
        spdlog::error("[anthropic] <- HTTP {} error: {}", status, msg);
        throw std::runtime_error(std::format(
            "AnthropicClient: HTTP {} - {}", status, msg));
    }

    RespJson parsed{};
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            parsed, resp->body()); e) {
        spdlog::error("[anthropic] <- parse failed: {} (body first 200c: {})",
                      glz::format_error(e, resp->body()),
                      resp->body().substr(0, 200));
        throw std::runtime_error("AnthropicClient: response parse failed: " +
                                 glz::format_error(e, resp->body()));
    }

    for (auto& blk : parsed.content)
        if (blk.type == "text") {
            spdlog::info("[anthropic] <- extracted text block, {}c", blk.text.size());
            co_return blk.text;
        }

    spdlog::error("[anthropic] <- no text content in response (blocks={})",
                  parsed.content.size());
    throw std::runtime_error("AnthropicClient: no text content in response");
}

} // namespace astraea
