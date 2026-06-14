#include "astraea/retriever.hpp"
#include <format>
#include <glaze/glaze.hpp>
#include <stdexcept>

// JSON structs must have external linkage for glaze reflection.
// Anonymous-namespace types are TU-local ([basic.link]) and break
// glz::detail::external<T> on Clang 18 + libc++.
namespace astraea::detail::retriever_json {

// Qdrant "match" clause: {"key":"court_name","match":{"any":["TT"]}}
struct MatchAny   { std::vector<std::string> any; };
struct MustClause { std::string key; MatchAny match; };
struct FilterJson { std::vector<MustClause> must; };

// Search request. filter is std::nullopt when no conditions apply; glaze
// skips nullopt members by default so the field is absent from the JSON.
struct SearchReq {
    std::vector<float> vector;
    int limit;
    float score_threshold;
    bool with_payload;
    std::optional<FilterJson> filter;
};

// Fetch-by-IDs request.
struct FetchReq {
    std::vector<std::string> ids;
    bool with_payload;
};

// Payload values in Qdrant can be any JSON type (string, number, array, etc.).
// Use glz::json_t so glaze accepts the full response without errors; the
// to_qdrant_point() helper then converts each value to a plain string.
struct PointResult {
    std::string id;
    std::optional<float> score;  // absent on fetch (non-search) endpoints
    std::unordered_map<std::string, glz::json_t> payload;
};

struct SearchResp {
    std::vector<PointResult> result;
};

} // namespace astraea::detail::retriever_json

namespace astraea {

namespace {

using namespace astraea::detail::retriever_json;

drogon::HttpClientPtr make_client(const std::string& url) {
    auto c = drogon::HttpClient::newHttpClient(url);
    if (!c) throw std::invalid_argument("VectorStore: invalid qdrant_url: " + url);
    return c;
}

FilterJson to_filter_json(const QdrantFilter& f) {
    FilterJson jf;
    jf.must.reserve(f.must.size());
    for (const auto& cond : f.must)
        jf.must.push_back({cond.field, {cond.values}});
    return jf;
}

// Convert a json_t payload value to a plain string for QdrantPoint.payload.
// Strings are unwrapped; numbers/booleans/arrays are serialised to JSON text.
std::string json_val_to_string(const glz::json_t& v) {
    if (const auto* s = v.get_if<std::string>()) return *s;
    auto dumped = v.dump();
    return dumped ? std::move(*dumped) : std::string{};
}

QdrantPoint to_qdrant_point(PointResult&& pt) {
    QdrantPoint out;
    out.id    = std::move(pt.id);
    out.score = pt.score.value_or(0.0f);
    for (auto& [k, v] : pt.payload)
        out.payload.emplace(k, json_val_to_string(v));
    return out;
}

drogon::HttpRequestPtr make_json_post(std::string path, std::string body) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath(std::move(path));
    req->setBody(std::move(body));
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    return req;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// VectorStore
// ---------------------------------------------------------------------------

VectorStore::VectorStore(std::string qdrant_url,
                         std::string collection,
                         std::string court_name,
                         double timeout_s)
    : _url(std::move(qdrant_url))
    , _collection(std::move(collection))
    , _court_name(std::move(court_name))
    , _timeout_s(timeout_s)
    , _client(make_client(_url))
{}

drogon::Task<std::vector<QdrantPoint>> VectorStore::search(
    std::vector<float> query_vector,
    int top_k,
    float min_score,
    std::optional<QdrantFilter> filter) const
{
    using namespace astraea::detail::retriever_json;

    std::optional<FilterJson> fj;
    if (filter) fj = to_filter_json(*filter);

    std::string body;
    if (auto we = glz::write_json(SearchReq{
            std::move(query_vector), top_k, min_score,
            /*with_payload=*/true, std::move(fj),
        }, body); we)
        throw std::runtime_error("qdrant search: request serialization failed");

    const auto path = std::format("/collections/{}/points/search", _collection);
    auto resp = co_await _client->sendRequestCoro(make_json_post(path, std::move(body)), _timeout_s);
    if (static_cast<int>(resp->statusCode()) != 200)
        throw std::runtime_error("qdrant search: HTTP " +
                                 std::to_string(static_cast<int>(resp->statusCode())));

    SearchResp parsed{};
    if (auto pe = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, resp->body()); pe)
        throw std::runtime_error("qdrant search: parse failed: " +
                                 glz::format_error(pe, resp->body()));

    std::vector<QdrantPoint> out;
    out.reserve(parsed.result.size());
    for (auto& pt : parsed.result)
        out.push_back(to_qdrant_point(std::move(pt)));
    co_return out;
}

drogon::Task<std::vector<QdrantPoint>> VectorStore::filtered_search(
    std::vector<float> query_vector,
    int top_k,
    float min_score,
    std::optional<QdrantFilter> extra_filter) const
{
    // Build a combined filter that ANDs court_name and all extra conditions
    // into a single Qdrant "must" array. Both conditions apply simultaneously.
    QdrantFilter combined;
    if (!_court_name.empty())
        combined.must.push_back({"court_name", {_court_name}});
    if (extra_filter)
        combined.must.insert(combined.must.end(),
                             extra_filter->must.begin(),
                             extra_filter->must.end());

    co_return co_await search(std::move(query_vector), top_k, min_score,
                              combined.must.empty()
                                  ? std::nullopt
                                  : std::make_optional(std::move(combined)));
}

drogon::Task<std::vector<QdrantPoint>> VectorStore::fetch(
    std::vector<std::string> ids) const
{
    using namespace astraea::detail::retriever_json;

    std::string body;
    if (auto we = glz::write_json(FetchReq{std::move(ids), /*with_payload=*/true}, body); we)
        throw std::runtime_error("qdrant fetch: request serialization failed");

    const auto path = std::format("/collections/{}/points", _collection);
    auto resp = co_await _client->sendRequestCoro(make_json_post(path, std::move(body)), _timeout_s);
    if (static_cast<int>(resp->statusCode()) != 200)
        throw std::runtime_error("qdrant fetch: HTTP " +
                                 std::to_string(static_cast<int>(resp->statusCode())));

    SearchResp parsed{};
    if (auto pe = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, resp->body()); pe)
        throw std::runtime_error("qdrant fetch: parse failed: " +
                                 glz::format_error(pe, resp->body()));

    std::vector<QdrantPoint> out;
    out.reserve(parsed.result.size());
    for (auto& pt : parsed.result)
        out.push_back(to_qdrant_point(std::move(pt)));
    co_return out;
}

} // namespace astraea
