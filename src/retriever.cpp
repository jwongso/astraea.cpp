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
    //  "with_payload":true,"filter":<qdrant filter json>}
    // Parse response via glaze, return QdrantPoint vector.
    co_return {};
}

drogon::Task<std::vector<QdrantPoint>> VectorStore::filtered_search(
    std::vector<float> query_vector,
    int top_k,
    float min_score,
    std::optional<QdrantFilter> extra_filter) const
{
    // Inject court_name as a must-match condition when set.
    if (!_court_name.empty() && !extra_filter) {
        extra_filter = QdrantFilter{"court_name", {_court_name}};
    }
    co_return co_await search(std::move(query_vector), top_k, min_score,
                              std::move(extra_filter));
}

drogon::Task<std::vector<QdrantPoint>> VectorStore::fetch(
    std::vector<std::string> /*ids*/) const
{
    // TODO(Phase3): POST to /collections/{_collection}/points with
    // {"ids":ids,"with_payload":true}
    co_return {};
}

} // namespace astraea
