#include "astraea/reranker.hpp"
#include <glaze/glaze.hpp>
#include <stdexcept>

// JSON structs must have external linkage for glaze reflection.
// Anonymous-namespace types are TU-local ([basic.link]) and break
// glz::detail::external<T> on Clang 18 + libc++.
namespace astraea::detail::reranker_json {

struct RerankReq {
    std::string model;
    std::string query;
    std::vector<std::string> documents;
};

struct RerankResultJson {
    int index = 0;
    float relevance_score = 0.0f;
};

struct RerankResp {
    std::vector<RerankResultJson> results;
};

} // namespace astraea::detail::reranker_json

namespace astraea {

namespace {

using namespace astraea::detail::reranker_json;

drogon::HttpClientPtr make_client(const std::string& url) {
    auto c = drogon::HttpClient::newHttpClient(url);
    if (!c) throw std::invalid_argument("Reranker: invalid base_url: " + url);
    return c;
}

} // anonymous namespace

Reranker::Reranker(std::string base_url, std::string model, bool enabled,
                   double timeout_s)
    : _base_url(std::move(base_url))
    , _model(std::move(model))
    , _enabled(enabled)
    , _timeout_s(timeout_s)
    , _client(make_client(_base_url))
{}

drogon::Task<std::vector<RerankCandidate>> Reranker::score_and_filter(
    std::string query,
    std::vector<RerankCandidate> candidates,
    float min_score) const
{
    using namespace astraea::detail::reranker_json;

    if (!_enabled) co_return candidates;

    std::vector<std::string> docs;
    docs.reserve(candidates.size());
    for (const auto& c : candidates)
        docs.push_back(c.text);

    std::string body;
    if (auto we = glz::write_json(RerankReq{_model, query, std::move(docs)}, body); we)
        throw std::runtime_error("rerank: request serialization failed");

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath("/v1/rerank");
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    // On any network/parse error fall back gracefully: return all candidates
    // as-is. The reranker is non-critical; degraded output beats an error.
    drogon::HttpResponsePtr resp;
    try {
        resp = co_await _client->sendRequestCoro(req, _timeout_s);
    } catch (...) {
        co_return candidates;
    }
    if (static_cast<int>(resp->statusCode()) != 200) co_return candidates;

    RerankResp parsed{};
    if (auto pe = glz::read_json(parsed, resp->body()); pe) co_return candidates;

    // Map scores back to candidates by index, then filter.
    for (const auto& r : parsed.results) {
        if (r.index >= 0 && static_cast<size_t>(r.index) < candidates.size())
            candidates[r.index].score = r.relevance_score;
    }

    std::vector<RerankCandidate> out;
    out.reserve(candidates.size());
    for (auto& c : candidates) {
        if (c.forced || c.score >= min_score)
            out.push_back(std::move(c));
    }
    co_return out;
}

} // namespace astraea
