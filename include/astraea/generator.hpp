#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

// Called with each token as it arrives from the LLM stream.
using TokenCallback = std::function<void(std::string_view token)>;

// Message role + content pair for the chat completions API.
using ChatMessage = std::pair<std::string, std::string>; // {role, content}

// Streaming LLM client backed by llama-server /v1/chat/completions.
// Parses SSE data frames, extracts choices[0].delta.content, and calls
// on_token for each token so the Drogon SSE handler can write to the
// socket immediately with zero buffering.
class Generator {
public:
    Generator(std::string base_url,
              std::string model,
              int max_tokens   = 2500,
              float temperature = 0.2f);

    // Stream a completion. on_token is called once per token in arrival order.
    // Returns the full concatenated response after the stream closes.
    drogon::Task<std::string> generate_stream(
        std::vector<ChatMessage> messages,
        TokenCallback on_token = nullptr) const;

    // Non-streaming completion. Useful for query rewrite (short outputs).
    drogon::Task<std::string> generate(
        std::vector<ChatMessage> messages) const;

    const std::string& model() const noexcept { return _model; }

private:
    std::string _base_url;
    std::string _model;
    int _max_tokens;       // used in Phase3 generate request body
    float _temperature;    // used in Phase3 generate request body
    drogon::HttpClientPtr _client;
};

} // namespace astraea
