#pragma once
#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>
#include <string>
#include <string_view>
#include <vector>

namespace astraea {

struct AnthropicMessage {
    std::string role;     // "user" or "assistant"
    std::string content;
};

/// Non-streaming client for the Anthropic Messages API (/v1/messages).
/// Distinct from Generator which targets the OpenAI-compatible llama-server
/// endpoint. Used by llm_wiki and any other feature needing direct Claude access.
class AnthropicClient {
public:
    AnthropicClient(std::string api_key,
                    std::string model   = "claude-sonnet-4-6",
                    int         max_tok = 8192,
                    double      timeout = 120.0);

    // Single-turn: system + one user message.
    drogon::Task<std::string> complete(std::string_view system_prompt,
                                       std::string_view user_prompt);

    // Multi-turn: system + message history (must end with a user message).
    drogon::Task<std::string> complete(std::string_view               system_prompt,
                                       std::vector<AnthropicMessage>  messages);

    const std::string& model() const { return _model; }

private:
    std::string           _api_key;
    std::string           _model;
    int                   _max_tok;
    double                _timeout;
    drogon::HttpClientPtr _client;   // persistent HTTPS client to api.anthropic.com
};

} // namespace astraea
