#include "astraea/pipeline.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace astraea {

namespace {

// Score multipliers for MANUAL court secondary sources.
// Authoritative sources (legislation, case_law, official_policy) get no discount.
const std::unordered_map<std::string, float> MANUAL_DISCOUNTS = {
    {"law_review",              0.85f},
    {"advocacy_submission",     0.85f},
    {"community_legal_guidance",0.85f},
    {"commercial_commentary",   0.80f},
};

void apply_manual_discounts(std::vector<QdrantPoint>& hits) {
    for (auto& h : hits) {
        auto court_it = h.payload.find("court");
        if (court_it == h.payload.end() || court_it->second != "MANUAL") continue;
        auto type_it = h.payload.find("source_type");
        if (type_it == h.payload.end()) continue;
        auto disc_it = MANUAL_DISCOUNTS.find(type_it->second);
        if (disc_it != MANUAL_DISCOUNTS.end())
            h.score *= disc_it->second;
    }
}

// Deduplicate by id, keep highest score per id, return sorted top-k.
std::vector<QdrantPoint> deduplicate(std::vector<QdrantPoint> hits, int top_k) {
    std::unordered_map<std::string, QdrantPoint> seen;
    for (auto& h : hits) {
        auto [it, inserted] = seen.emplace(h.id, h);
        if (!inserted && h.score > it->second.score)
            it->second = std::move(h);
    }
    std::vector<QdrantPoint> result;
    result.reserve(seen.size());
    for (auto& [id, pt] : seen)
        result.push_back(std::move(pt));
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });
    if (static_cast<int>(result.size()) > top_k)
        result.resize(top_k);
    return result;
}

std::unordered_set<std::string> word_set(const std::string& text) {
    std::unordered_set<std::string> words;
    std::istringstream iss(text);
    std::string w;
    while (iss >> w) {
        std::transform(w.begin(), w.end(), w.begin(), ::tolower);
        words.insert(w);
    }
    return words;
}

float jaccard(const std::unordered_set<std::string>& a,
              const std::unordered_set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0f;
    int inter = 0;
    for (const auto& w : a)
        if (b.count(w)) ++inter;
    const int uni = static_cast<int>(a.size() + b.size()) - inter;
    return uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 0.0f;
}

// Maximal Marginal Relevance - mirrors Python _mmr_select.
std::vector<QdrantPoint> mmr_select(std::vector<QdrantPoint> hits,
                                     int top_k,
                                     float lambda = 0.6f) {
    std::vector<QdrantPoint> selected;
    std::vector<QdrantPoint> remaining = std::move(hits);
    while (static_cast<int>(selected.size()) < top_k && !remaining.empty()) {
        if (selected.empty()) {
            selected.push_back(std::move(remaining.front()));
            remaining.erase(remaining.begin());
            continue;
        }
        int best_i = 0;
        float best = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(remaining.size()); ++i) {
            const auto& h = remaining[i];
            auto text_it = h.payload.find("text");
            auto h_words = word_set(text_it != h.payload.end() ? text_it->second : "");
            float max_sim = 0.0f;
            for (const auto& s : selected) {
                auto s_it = s.payload.find("text");
                auto s_words = word_set(s_it != s.payload.end() ? s_it->second : "");
                max_sim = std::max(max_sim, jaccard(h_words, s_words));
            }
            const float score = lambda * h.score - (1.0f - lambda) * max_sim;
            if (score > best) { best = score; best_i = i; }
        }
        selected.push_back(std::move(remaining[best_i]));
        remaining.erase(remaining.begin() + best_i);
    }
    return selected;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RAGPipeline
// ---------------------------------------------------------------------------

RAGPipeline::RAGPipeline(std::string qdrant_url,
                         std::string collection,
                         std::string embed_base_url,
                         std::string embed_model,
                         std::string llm_base_url,
                         std::string llm_model,
                         std::string rerank_base_url,
                         std::string rerank_model,
                         std::string court_name,
                         int embed_dims,
                         int llm_max_tokens,
                         float llm_temperature,
                         bool enable_reranker)
    : _embedder(std::move(embed_base_url), std::move(embed_model), embed_dims)
    , _store(std::move(qdrant_url), std::move(collection), std::move(court_name))
    , _generator(std::move(llm_base_url), std::move(llm_model), llm_max_tokens, llm_temperature)
    , _reranker(std::move(rerank_base_url), std::move(rerank_model), enable_reranker)
{}

drogon::Task<RetrieveResult> RAGPipeline::retrieve(
    std::string question,
    int top_k,
    float min_score,
    int min_chunks,
    bool use_mmr) const
{
    auto query_vector = co_await _embedder.embed(question);
    // filtered_search injects the court_name stored in the VectorStore constructor.
    auto raw = co_await _store.filtered_search(std::move(query_vector), top_k * 3);

    if (raw.empty()) co_return RetrieveResult{};

    apply_manual_discounts(raw);
    auto hits = deduplicate(std::move(raw), top_k * 2);

    if (use_mmr)
        hits = mmr_select(std::move(hits), top_k);
    else if (static_cast<int>(hits.size()) > top_k)
        hits.resize(top_k);

    if (min_score > 0.0f) {
        hits.erase(std::remove_if(hits.begin(), hits.end(),
                                  [min_score](const QdrantPoint& h) {
                                      return h.score < min_score;
                                  }),
                   hits.end());
    }
    if (static_cast<int>(hits.size()) < min_chunks)
        co_return RetrieveResult{};

    RetrieveResult result;
    result.texts.reserve(hits.size());
    result.sources.reserve(hits.size());
    for (const auto& h : hits) {
        auto it = h.payload.find("text");
        result.texts.push_back(it != h.payload.end() ? it->second : "");
        result.sources.push_back(h);
    }
    co_return result;
}

drogon::Task<std::vector<float>> RAGPipeline::embed(std::string text) const {
    co_return co_await _embedder.embed(std::move(text));
}

} // namespace astraea
