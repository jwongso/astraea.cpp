#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

// Role + content pair for the chat completions API.
// Using a struct rather than pair<string,string> keeps call sites self-documenting.
struct ChatMessage {
    std::string role;
    std::string content;
};

// Called with each token as it arrives from the LLM stream.
// Lifetime: the string_view is valid only for the duration of the callback.
// Phase 3 must ensure the SSE parse buffer is not moved or freed while
// any callback is in progress.
using TokenCallback = std::function<void(std::string_view token)>;

// LLM client backed by llama-server /v1/chat/completions.
// Parses SSE data frames, extracts choices[0].delta.content, and calls
// on_token for each token.
//
// Streaming model (Phase 6D and later): generate_stream() bypasses Drogon's
// buffering HttpClient and goes through astraea::detail::LlmStreamSession
// (built on raw trantor::TcpClient). TokenCallback fires for each token as
// the LLM emits it - safe to wire directly to a Drogon SSE response.
// The non-streaming generate() still uses Drogon's HttpClient because the
// response is single-shot and small.
class Generator {
public:
    // enable_thinking: forwarded as chat_template_kwargs.enable_thinking on
    // every chat-completions request. Maps directly to the Qwen3-family
    // thinking mode (when true, the model emits a <think>...</think> block
    // before the answer; when false, it skips think tokens entirely). Default
    // true matches Python core/generator.py's `thinking: bool = True` default
    // for generation; pass false for short, deterministic calls like query
    // rewrite where think tokens are pure waste.
    Generator(std::string base_url,
              std::string model,
              int max_tokens     = 2500,
              float temperature  = 0.2f,
              bool enable_thinking = true);

    // Issue a streaming completion. on_token is called per LLM token as
    // each SSE chunk arrives (true per-token streaming via trantor::TcpClient;
    // see Phase 6D). Returns the full concatenated response on success.
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
    int _max_tokens;
    float _temperature;
    bool _enable_thinking;
    drogon::HttpClientPtr _client;
};

} // namespace astraea
