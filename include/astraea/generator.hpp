#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>

namespace astraea {

namespace detail { class LlmTcpPool; }  // fwd decl - full type in detail/llm_tcp_pool.hpp

/// @brief Role and content pair for the OpenAI chat completions API.
///
/// Using a named struct rather than pair<string,string> keeps call sites
/// self-documenting. Python parity: core/generator.py ChatMessage.
struct ChatMessage {
    std::string role; ///< "system", "user", or "assistant".
    std::string content; ///< Message body text.
};

/// @brief Callback invoked once per LLM token as it arrives from the stream.
///
/// The string_view is valid only for the duration of the callback; the caller
/// must copy it if longer lifetime is needed. Fires on the trantor event loop
/// thread that owns the LLM-side socket.
using TokenCallback = std::function<void(std::string_view token)>;

/// @brief LLM client backed by a llama-server /v1/chat/completions endpoint.
///
/// Streaming model (Phase 6D and later): generate_stream() bypasses Drogon's
/// buffering HttpClient and uses astraea::detail::LlmStreamSession built on
/// raw trantor::TcpClient. TokenCallback fires per token as the LLM emits it,
/// safe to wire directly to a Drogon SSE response. The non-streaming generate()
/// still uses Drogon's HttpClient because query-rewrite responses are small
/// and single-shot.
///
/// Thread-safe: all public methods may be called from multiple event loops.
class Generator {
public:
    /// @brief Construct a Generator.
    ///
    /// enable_thinking is forwarded as chat_template_kwargs.enable_thinking.
    /// Set false (default) for Qwen3 to suppress the think-block
    /// that inflates TTFT; set true only when reasoning quality outweighs
    /// the latency cost. Non-Qwen3 backends that reject unknown
    /// chat_template_kwargs should also use false.
    Generator(std::string base_url,
              std::string model,
              int max_tokens     = 2500,
              float temperature  = 0.2f,
              bool enable_thinking = true,
              double stream_idle_timeout_s = 300.0);

    /// @brief Issue a streaming chat completion.
    ///
    /// on_token is called per token as each SSE chunk arrives (true per-token
    /// streaming via trantor::TcpClient). Returns the full concatenated response
    /// on success. When `cancelled` is set to true by the token callback (e.g.
    /// because the downstream client disconnected), the session aborts the
    /// upstream LLM connection after the next token, releasing the semaphore
    /// without waiting for [DONE].
    /// @param temperature_override  When >= 0, overrides the instance temperature.
    ///                              Only accepted from eval/debug paths; must be in {0.0, 0.1, 0.2}.
    ///                              Pass -1 (default) to use the instance temperature.
    drogon::Task<std::string> generate_stream(
        std::vector<ChatMessage> messages,
        TokenCallback on_token = nullptr,
        std::shared_ptr<std::atomic<bool>> cancelled = nullptr,
        float temperature_override = -1.0f) const;

    /// @brief Non-streaming chat completion; suitable for short deterministic calls.
    ///
    /// Used for query rewrite where the output is a single sentence and
    /// streaming latency savings do not apply.
    drogon::Task<std::string> generate(
        std::vector<ChatMessage> messages) const;

    const std::string& model() const noexcept { return _model; }

    /// @brief Expose the underlying TCP pool for /healthz idle-client count introspection.
    ///
    /// Nullable; returns nullptr before the pool has been constructed or when
    /// pooling is disabled. Callers must null-check before dereferencing.
    const detail::LlmTcpPool* stream_pool() const noexcept { return _stream_pool.get(); }

private:
    std::string _base_url;
    std::string _model;
    int _max_tokens;
    float _temperature;
    bool _enable_thinking;
    double _stream_idle_timeout_s;
    drogon::HttpClientPtr _client;
    // Pool of idle trantor::TcpClient instances shared across all calls to
    // generate_stream() from this Generator. Shared_ptr because each
    // LlmStreamSession captures the pointer; the Generator outlives every
    // session it spawns (Generator is constructed once at startup).
    // shared_ptr also lets the rewrite Generator and the main Generator
    // share a pool via copy-construct if we ever want unified pooling.
    std::shared_ptr<detail::LlmTcpPool> _stream_pool;
};

} // namespace astraea
