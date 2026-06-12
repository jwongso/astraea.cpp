#include "astraea/generator.hpp"

namespace astraea {

Generator::Generator(std::string base_url,
                     std::string model,
                     int max_tokens,
                     float temperature)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _max_tokens(max_tokens)
    , _temperature(temperature)
    , _client(drogon::HttpClient::newHttpClient(_base_url))
{}

drogon::Task<std::string> Generator::generate_stream(
    std::vector<ChatMessage> /*messages*/,
    TokenCallback /*on_token*/) const
{
    // TODO(Phase3): POST {"model":_model,"messages":messages,"stream":true,
    //   "max_tokens":_max_tokens,"temperature":_temperature}
    // to /v1/chat/completions. Parse SSE data frames, extract
    // choices[0].delta.content, call on_token for each non-empty token.
    // Return full concatenated response when [DONE] is received.
    co_return {};
}

drogon::Task<std::string> Generator::generate(
    std::vector<ChatMessage> messages) const
{
    co_return co_await generate_stream(std::move(messages), nullptr);
}

} // namespace astraea
