#include "astraea/retriever.hpp"

namespace astraea {

VectorStore::VectorStore(std::string qdrant_url,
                         std::string collection,
                         std::string court_name)
    : _url(std::move(qdrant_url))
    , _collection(std::move(collection))
    , _court_name(std::move(court_name))
    , _client(drogon::HttpClient::newHttpClient(_url))
{}

drogon::Task<std::vector<QdrantPoint>> VectorStore::search(
    std::vector<float> /*query_vector*/,
    int /*top_k*/,
    float /*min_score*/,
    std::optional<QdrantFilter> /*filter*/) const
{
    // TODO(Phase3): POST to /collections/{_collection}/points/search with
    // {"vector":query_vector,"limit":top_k,"score_threshold":min_score,
    //  "with_payload":true,"filter":{"must":[{"key":c.field,"match":{"any":c.values}}
    //  for c in filter->must]}}
    // Parse response via glaze, return QdrantPoint vector.
    co_return {};
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
    std::vector<std::string> /*ids*/) const
{
    // TODO(Phase3): POST to /collections/{_collection}/points with
    // {"ids":ids,"with_payload":true}
    co_return {};
}

} // namespace astraea
