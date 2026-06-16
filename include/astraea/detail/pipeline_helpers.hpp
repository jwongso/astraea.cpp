#pragma once
// Internal helpers for pipeline.cpp and anchor.cpp — also included by
// test_pipeline_helpers.cpp. All functions are pure (no I/O, no Drogon)
// and therefore testable without mocks.
#include "astraea/retriever_types.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace astraea::detail {

/// @brief Extract a string payload field from a QdrantPoint; returns an empty string when the key is absent.
inline const std::string& payload_field(const QdrantPoint& pt, const std::string& key) {
    static const std::string empty;
    auto it = pt.payload.find(key);
    return it != pt.payload.end() ? it->second : empty;
}

/// @brief Score multipliers applied to MANUAL court secondary sources.
///
/// Authoritative sources (case_law, official_policy, legislation) receive no
/// discount. Secondary source types are penalised to reduce their ranking
/// relative to primary sources at the same vector similarity.
inline const std::unordered_map<std::string, float>& manual_discounts() {
    static const std::unordered_map<std::string, float> kMap = {
        {"law_review",              0.85f},
        {"advocacy_submission",     0.85f},
        {"community_legal_guidance",0.85f},
        {"commercial_commentary",   0.80f},
    };
    return kMap;
}

inline void apply_manual_discounts(std::vector<QdrantPoint>& hits) {
    const auto& dm = manual_discounts();
    for (auto& h : hits) {
        auto court_it = h.payload.find("court");
        if (court_it == h.payload.end() || court_it->second != "MANUAL") continue;
        auto type_it = h.payload.find("source_type");
        if (type_it == h.payload.end()) continue;
        auto disc_it = dm.find(type_it->second);
        if (disc_it != dm.end())
            h.score *= disc_it->second;
    }
}

/// @brief Deduplicate hits by id, keep the highest score per id, sort descending, and cap at top_k.
inline std::vector<QdrantPoint> deduplicate(std::vector<QdrantPoint> hits, int top_k) {
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

inline std::unordered_set<std::string> word_set(const std::string& text) {
    std::unordered_set<std::string> words;
    std::istringstream iss(text);
    std::string w;
    while (iss >> w) {
        std::transform(w.begin(), w.end(), w.begin(), ::tolower);
        words.insert(w);
    }
    return words;
}

inline float jaccard(const std::unordered_set<std::string>& a,
                     const std::unordered_set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0f;
    int inter = 0;
    for (const auto& w : a)
        if (b.count(w)) ++inter;
    const int uni = static_cast<int>(a.size() + b.size()) - inter;
    return uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 0.0f;
}

/// @brief Maximal Marginal Relevance selection balancing relevance against redundancy.
///
/// Selected-item word sets are cached and reused across iterations to avoid
/// O(n * top_k) re-tokenisation. lambda=0.6 weights relevance over diversity.
inline std::vector<QdrantPoint> mmr_select(
    std::vector<QdrantPoint> hits, int top_k, float lambda = 0.6f)
{
    std::vector<QdrantPoint> selected;
    std::vector<std::unordered_set<std::string>> sel_words; // cached, parallel to selected
    std::vector<QdrantPoint> remaining = std::move(hits);

    while (static_cast<int>(selected.size()) < top_k && !remaining.empty()) {
        if (selected.empty()) {
            auto it = remaining.front().payload.find("text");
            sel_words.push_back(word_set(it != remaining.front().payload.end() ? it->second : ""));
            selected.push_back(std::move(remaining.front()));
            remaining.erase(remaining.begin());
            continue;
        }
        int best_i = 0;
        float best = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < static_cast<int>(remaining.size()); ++i) {
            const auto& h = remaining[i];
            auto it = h.payload.find("text");
            const auto h_words = word_set(it != h.payload.end() ? it->second : "");
            float max_sim = 0.0f;
            for (const auto& sw : sel_words)          // sel_words: computed once, reused
                max_sim = std::max(max_sim, jaccard(h_words, sw));
            const float score = lambda * h.score - (1.0f - lambda) * max_sim;
            if (score > best) { best = score; best_i = i; }
        }
        auto it = remaining[best_i].payload.find("text");
        sel_words.push_back(word_set(it != remaining[best_i].payload.end() ? it->second : ""));
        selected.push_back(std::move(remaining[best_i]));
        remaining.erase(remaining.begin() + best_i);
    }
    return selected;
}

} // namespace astraea::detail
