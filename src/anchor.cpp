#include "astraea/anchor.hpp"
#include "astraea/routing.hpp"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace astraea {

namespace {

// Minimum cosine similarity for an unprompted MANUAL guidance chunk to be
// injected. Set below typical tribunal case scores (~0.83) so relevant
// guidance always surfaces.
constexpr float GUIDANCE_THRESHOLD = 0.75f;

// Source types treated as authoritative official guidance.
// Excludes law_review / advocacy / community_legal / commercial (have score discounts).
const std::vector<std::string> GUIDANCE_SOURCE_TYPES = {
    "official_guidance",
    "official_policy",
};

// Returns true if the case_id prefix (before the first '/') contains "LEG"
// (case-insensitive). Identifies legislation chunks in mixed collections.
bool is_leg_chunk(std::string_view case_id) {
    const auto slash = case_id.find('/');
    if (slash == std::string_view::npos) return false;
    const auto prefix = case_id.substr(0, slash);
    return std::search(prefix.begin(), prefix.end(),
                       std::string_view("LEG").begin(), std::string_view("LEG").end(),
                       [](char a, char b) {
                           return std::toupper(static_cast<unsigned char>(a)) ==
                                  std::toupper(static_cast<unsigned char>(b));
                       }) != prefix.end();
}

// Extract payload field, returning empty string when absent.
const std::string& payload_field(const QdrantPoint& pt, const std::string& key) {
    static const std::string empty;
    auto it = pt.payload.find(key);
    return it != pt.payload.end() ? it->second : empty;
}

// Remove the first occurrence of id from vec (erases by value).
void erase_by_id(std::vector<QdrantPoint>& vec, const std::string& id) {
    auto it = std::find_if(vec.begin(), vec.end(),
                           [&id](const QdrantPoint& p) { return p.id == id; });
    if (it != vec.end()) vec.erase(it);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// retrieve_anchor
// ---------------------------------------------------------------------------

drogon::Task<AnchorResult> retrieve_anchor(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    VectorStore* leg_store,
    const JurisdictionBase& jurisdiction)
{
    if (!leg_store) co_return AnchorResult{};

    try {
        // --- route decision (no network call) ---
        const auto decision = build_route_decision(
            original_question.empty() ? question : original_question,
            question,
            jurisdiction.routes());

        // --- embed question for vector search ---
        auto query_vector = co_await pipeline.embedder().embed(question);

        // --- legislation retrieval: federated per-Act or single global ---
        std::vector<QdrantPoint> raw;
        const auto& leg_srcs = jurisdiction.leg_sources();
        if (!leg_srcs.empty()) {
            // One search per registered Act with per-source top_k quotas.
            // NOTE(Phase5): run sequentially; replace with drogon::when_all
            // once the generic vector-of-tasks form is confirmed stable.
            for (const auto& src : leg_srcs) {
                const int tk = decision.boosted_act_ids.count(src.act_id)
                             ? src.boost_top_k : src.default_top_k;
                QdrantFilter filt;
                filt.must.push_back({"court_name", {src.court_name}});
                auto batch = co_await leg_store->search(query_vector, tk, 0.0f, filt);
                raw.insert(raw.end(),
                           std::make_move_iterator(batch.begin()),
                           std::make_move_iterator(batch.end()));
            }
        } else {
            raw = co_await leg_store->search(query_vector, 12);
        }

        // --- route injection: forced sections go to the front ---
        // Synthetic embed calls go through Embedder::embed_synth() which
        // maintains an in-memory cache; warm() is called at startup.
        std::vector<std::string> injected_ids;
        std::vector<QdrantPoint> injections;
        std::unordered_set<std::string> seen_inject;
        const std::unordered_set<std::string> forced_set(
            decision.forced_sections.begin(), decision.forced_sections.end());

        // Build court_name filter for synth searches (leg chunk prefixes only).
        std::unordered_set<std::string> leg_court_prefixes;
        for (const auto& sid : decision.forced_sections) {
            const auto slash = sid.find('/');
            if (slash == std::string::npos) continue;
            const std::string prefix(sid.substr(0, slash));
            std::string upper = prefix;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (upper.find("LEG") != std::string::npos)
                leg_court_prefixes.insert(prefix);
        }

        for (const auto& synth_q : decision.leg_synthetic_queries) {
            auto synth_vec = co_await pipeline.embedder().embed_synth(synth_q);

            std::optional<QdrantFilter> synth_filter;
            if (!leg_court_prefixes.empty()) {
                QdrantFilter filt;
                filt.must.push_back({"court_name",
                    std::vector<std::string>(leg_court_prefixes.begin(), leg_court_prefixes.end())});
                synth_filter = std::move(filt);
            }

            const int synth_top_k = static_cast<int>(decision.forced_sections.size()) + 10;
            auto synth_raw = co_await leg_store->search(synth_vec, synth_top_k, 0.0f, synth_filter);

            for (auto& h : synth_raw) {
                if (forced_set.count(h.id) && !seen_inject.count(h.id)) {
                    erase_by_id(raw, h.id);
                    seen_inject.insert(h.id);
                    injected_ids.push_back(h.id);
                    injections.push_back(std::move(h));
                }
            }
        }

        // Fetch any forced section not yet seen via synth search.
        for (const auto& sid : decision.forced_sections) {
            if (seen_inject.count(sid)) continue;
            auto fetched = co_await leg_store->fetch({sid});
            if (!fetched.empty()) {
                erase_by_id(raw, fetched[0].id);
                seen_inject.insert(sid);
                injected_ids.push_back(sid);
                injections.push_back(std::move(fetched[0]));
            }
        }

        // Prepend forced sections.
        injections.insert(injections.end(),
                          std::make_move_iterator(raw.begin()),
                          std::make_move_iterator(raw.end()));
        raw = std::move(injections);

        // --- structural filters ---
        const std::string combined_q =
            normalize_query(original_question.empty()
                            ? question : (original_question + " " + question));
        const auto& lp = jurisdiction.low_priority_sections();

        raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const QdrantPoint& pt) {
            return !allow_section(pt.id, combined_q, lp);
        }), raw.end());

        if (!decision.leg_allow_list.empty()) {
            const std::unordered_set<std::string> allow_set(
                decision.leg_allow_list.begin(), decision.leg_allow_list.end());
            raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const QdrantPoint& pt) {
                return is_leg_chunk(pt.id) && !allow_set.count(pt.id);
            }), raw.end());
        }

        // Keep only legislation chunks (prevents case decisions leaking in).
        raw.erase(std::remove_if(raw.begin(), raw.end(), [](const QdrantPoint& pt) {
            return !is_leg_chunk(pt.id);
        }), raw.end());

        // --- cross-encoder relevance gate ---
        if (pipeline.reranker().enabled() && !raw.empty()) {
            const std::unordered_set<std::string> inj_set(
                injected_ids.begin(), injected_ids.end());
            std::vector<RerankCandidate> candidates;
            candidates.reserve(raw.size());
            for (const auto& pt : raw) {
                RerankCandidate rc;
                rc.id    = pt.id;
                rc.text  = payload_field(pt, "text");
                rc.score = pt.score;
                rc.forced = inj_set.count(pt.id) > 0;
                candidates.push_back(std::move(rc));
            }

            try {
                auto ranked = co_await pipeline.reranker().score_and_filter(
                    question, std::move(candidates), jurisdiction.leg_ce_min_score());

                // Map surviving IDs back to QdrantPoints.
                std::unordered_map<std::string, QdrantPoint> pt_map;
                for (auto& pt : raw)
                    pt_map.emplace(pt.id, std::move(pt));

                raw.clear();
                for (const auto& rc : ranked) {
                    auto it = pt_map.find(rc.id);
                    if (it != pt_map.end())
                        raw.push_back(std::move(it->second));
                }
            } catch (...) {
                // CE gate failed - proceed with unfiltered candidates.
            }
        }

        // --- deduplicate and cap ---
        const int max_hits = injected_ids.empty()
                           ? 2
                           : std::max(3, static_cast<int>(injected_ids.size()));
        std::unordered_set<std::string> seen;
        std::vector<QdrantPoint> hits;
        for (auto& pt : raw) {
            if (seen.insert(pt.id).second) {
                hits.push_back(std::move(pt));
                if (static_cast<int>(hits.size()) >= max_hits) break;
            }
        }

        if (hits.empty()) co_return AnchorResult{};

        // --- build anchor text ---
        std::string anchor =
            "Relevant Act sections "
            "(legislative context - use for grounding section numbers only, "
            "do not cite with [SN] notation):";
        std::vector<QdrantPoint> leg_srcs_out;
        leg_srcs_out.reserve(hits.size());
        for (const auto& h : hits) {
            const auto& title = payload_field(h, "title");
            const auto& text  = payload_field(h, "text");
            anchor += "\n\n" + title + "\n" +
                      (text.size() > 600 ? text.substr(0, 600) : text);
            leg_srcs_out.push_back(h);
        }

        co_return AnchorResult{std::move(anchor), std::move(leg_srcs_out)};

    } catch (...) {
        co_return AnchorResult{};
    }
}

// ---------------------------------------------------------------------------
// augment_case_retrieval
// ---------------------------------------------------------------------------

drogon::Task<> augment_case_retrieval(
    std::string question,
    std::string retrieval_question,
    RAGPipeline& pipeline,
    const JurisdictionBase& jurisdiction,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources)
{
    const auto decision = build_route_decision(
        question, retrieval_question, jurisdiction.routes());
    if (decision.case_synthetic_queries.empty()) co_return;

    std::unordered_set<std::string> existing_ids;
    for (const auto& s : sources)
        existing_ids.insert(s.id);

    for (const auto& csq : decision.case_synthetic_queries) {
        auto extra = co_await pipeline.retrieve(csq, 5, 0.70f, 1, false);
        for (size_t i = 0; i < extra.texts.size() && sources.size() < 8; ++i) {
            if (existing_ids.insert(extra.sources[i].id).second) {
                context_texts.push_back(std::move(extra.texts[i]));
                sources.push_back(std::move(extra.sources[i]));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// retrieve_manual_guidance
// ---------------------------------------------------------------------------

drogon::Task<GuidanceResult> retrieve_manual_guidance(
    std::string question,
    std::string original_question,
    RAGPipeline& pipeline,
    const std::unordered_set<std::string>& existing_source_ids,
    const JurisdictionBase& jurisdiction)
{
    try {
        // Collect forced guidance doc IDs from all matched routes (deduped).
        const auto decision = build_route_decision(
            original_question.empty() ? question : original_question,
            question,
            jurisdiction.routes());

        const std::unordered_set<std::string> matched(
            decision.matched_intents.begin(), decision.matched_intents.end());

        std::vector<std::string> forced_ids;
        std::unordered_set<std::string> seen_forced;
        for (const auto& route : jurisdiction.routes()) {
            if (!matched.count(route.intent)) continue;
            for (const auto& gid : route.guidance_sources) {
                if (seen_forced.insert(gid).second)
                    forced_ids.push_back(gid);
            }
        }

        auto query_vec = co_await pipeline.embedder().embed(question);

        // Search for MANUAL guidance chunks (court=MANUAL AND source_type in list).
        QdrantFilter filt;
        filt.must.push_back({"court", {"MANUAL"}});
        filt.must.push_back({"source_type", GUIDANCE_SOURCE_TYPES});
        auto hits = co_await pipeline.store().search(query_vec, 10, 0.0f, filt);

        std::unordered_map<std::string, QdrantPoint> hits_by_id;
        for (auto& h : hits)
            hits_by_id.emplace(h.id, std::move(h));

        if (!forced_ids.empty()) {
            // Route-guided path: pick the highest-scored forced doc in vector results.
            const QdrantPoint* best_h = nullptr;
            float best_score = -1.0f;
            for (const auto& gid : forced_ids) {
                if (existing_source_ids.count(gid)) continue;
                auto it = hits_by_id.find(gid);
                if (it != hits_by_id.end() && it->second.score > best_score) {
                    best_h = &it->second;
                    best_score = it->second.score;
                }
            }
            if (best_h) {
                co_return GuidanceResult{
                    payload_field(*best_h, "text"),
                    *best_h,
                    "route_forced_vector",
                };
            }

            // Not in vector results - fetch the first available forced doc directly.
            for (const auto& gid : forced_ids) {
                if (existing_source_ids.count(gid)) continue;
                auto fetched = co_await pipeline.store().fetch({gid});
                if (!fetched.empty()) {
                    co_return GuidanceResult{
                        payload_field(fetched[0], "text"),
                        fetched[0],
                        "route_forced",
                    };
                }
            }
        }

        // No route-forced guidance (or all already retrieved): vector threshold.
        // hits is ordered by score descending from the search call.
        for (const auto& h : hits) {
            if (h.score < GUIDANCE_THRESHOLD) break;
            if (existing_source_ids.count(h.id)) continue;
            co_return GuidanceResult{payload_field(h, "text"), h, "vector_search"};
        }

        co_return GuidanceResult{};

    } catch (...) {
        co_return GuidanceResult{};
    }
}

// ---------------------------------------------------------------------------
// refine_retrieve
// ---------------------------------------------------------------------------

drogon::Task<> refine_retrieve(
    std::string original_question,
    std::string rewritten_question,
    RAGPipeline& pipeline,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources)
{
    std::unordered_set<std::string> existing_ids;
    for (const auto& s : sources)
        existing_ids.insert(s.id);

    // Deduplicate queries by lowercase-normalised form.
    std::vector<std::string> queries;
    {
        std::unordered_set<std::string> seen;
        for (const auto& q : {original_question, rewritten_question}) {
            std::string norm = q;
            std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);
            // Collapse runs of whitespace to a single space.
            bool in_ws = false;
            std::string collapsed;
            for (char c : norm) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!in_ws) { collapsed += ' '; in_ws = true; }
                } else {
                    collapsed += c; in_ws = false;
                }
            }
            if (seen.insert(collapsed).second)
                queries.push_back(q);
        }
    }

    std::vector<std::string> new_texts;
    std::vector<QdrantPoint> new_sources;

    for (const auto& query : queries) {
        auto extra = co_await pipeline.retrieve(query, 8, 0.65f, 1, false);
        for (size_t i = 0; i < extra.texts.size(); ++i) {
            if (existing_ids.insert(extra.sources[i].id).second) {
                new_texts.push_back(std::move(extra.texts[i]));
                new_sources.push_back(std::move(extra.sources[i]));
            }
        }
    }

    if (new_sources.empty()) co_return;

    // Combine, sort by score, cap at 6.
    std::vector<std::pair<QdrantPoint, std::string>> combined;
    combined.reserve(sources.size() + new_sources.size());
    for (size_t i = 0; i < sources.size(); ++i)
        combined.push_back({std::move(sources[i]), std::move(context_texts[i])});
    for (size_t i = 0; i < new_sources.size(); ++i)
        combined.push_back({std::move(new_sources[i]), std::move(new_texts[i])});

    std::sort(combined.begin(), combined.end(),
              [](const auto& a, const auto& b) {
                  return a.first.score > b.first.score;
              });
    if (combined.size() > 6) combined.resize(6);

    sources.clear(); context_texts.clear();
    for (auto& [src, txt] : combined) {
        sources.push_back(std::move(src));
        context_texts.push_back(std::move(txt));
    }
}

} // namespace astraea
