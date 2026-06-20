#include "astraea/anchor.hpp"
#include "astraea/detail/anchor_helpers.hpp"
#include "astraea/detail/pipeline_helpers.hpp"
#include "astraea/routing.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace astraea {

namespace {


// Source types treated as authoritative official guidance.
// Excludes law_review / advocacy / community_legal / commercial (have score discounts).
const std::vector<std::string> GUIDANCE_SOURCE_TYPES = {
    "official_guidance",
    "official_policy",
};

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
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed,
    const std::vector<float>* precomputed_vec)
{
    if (!leg_store) co_return AnchorResult{};

    const auto t_anchor = std::chrono::steady_clock::now();
    // Capture elapsed_ms on every return path so anchor_ms in the timing
    // event reflects real wall time even when we early-exit (empty Qdrant
    // result, exception). Prior to this lambda the catch and empty-hits
    // returns silently reported anchor_ms = 0 - exactly when the operator
    // most wants to see "Qdrant burned 200ms and returned nothing".
    auto elapsed = [&]() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t_anchor).count() / 1000.0;
    };
    try {
        // route decision: prefer the caller's precomputed one (skips an AC
        // scan). Storage for the local fallback must outlive the reference
        // so the decision is bound by const-ref either way.
        RouteDecision local_decision;
        const RouteDecision& decision = precomputed
            ? *precomputed
            : (local_decision = build_route_decision(
                  original_question.empty() ? question : original_question,
                  question,
                  table));

        // Use precomputed embedding when the caller already embedded the same
        // text for the corpus retrieve step - saves one embed server RTT.
        std::vector<float> query_vector = precomputed_vec
            ? *precomputed_vec
            : co_await pipeline.embedder().embed(question);

        // Legislation retrieval: all per-Act searches batched into one HTTP
        // round-trip via Qdrant's /points/search/batch endpoint.
        std::vector<QdrantPoint> raw;
        const auto& leg_srcs = jurisdiction.leg_sources();
        if (!leg_srcs.empty()) {
            std::vector<BatchSearchRequest> reqs;
            reqs.reserve(leg_srcs.size());
            for (const auto& src : leg_srcs) {
                const int tk = decision.boosted_act_ids.count(src.act_id)
                             ? src.boost_top_k : src.default_top_k;
                QdrantFilter filt;
                filt.must.push_back({"court_name", {src.court_name}});
                reqs.push_back({query_vector, tk, 0.0f, std::move(filt)});
            }
            auto batches = co_await leg_store->batch_search(std::move(reqs));
            for (auto& b : batches)
                raw.insert(raw.end(),
                           std::make_move_iterator(b.begin()),
                           std::make_move_iterator(b.end()));
        } else {
            raw = co_await leg_store->search(query_vector, 12);
        }

        // Route injection: forced sections go to the front.
        // Synthetic embed calls go through embed_synth() which hits the
        // in-memory cache populated by warm() at startup (~microseconds).
        // All synth Qdrant searches are batched into one HTTP round-trip.
        //
        // injected_ids stores the Qdrant point UUIDs of injected chunks so the
        // CE gate can mark them as rc.forced = true and exempt them from score
        // filtering. (We must push UUIDs, not case_id strings, because the CE
        // gate compares against pt.id which is always a UUID.)
        std::vector<std::string> injected_ids;
        std::vector<QdrantPoint> injections;
        std::unordered_set<std::string> seen_inject; // tracks case_ids to dedup
        const std::unordered_set<std::string> forced_set(
            decision.forced_sections.begin(), decision.forced_sections.end());

        if (!decision.leg_synthetic_queries.empty()) {
            // Collect all synth embed vectors (cache hits - fast), then batch
            // the Qdrant searches into a single HTTP call.
            // No court_name filter: leg_store is a legislation-only collection;
            // filtering by the case_id prefix (e.g. "NZLEG") would not match the
            // full act name stored in the payload and would suppress forced sections.
            const int synth_top_k = static_cast<int>(decision.forced_sections.size()) + 10;

            std::vector<BatchSearchRequest> synth_reqs;
            synth_reqs.reserve(decision.leg_synthetic_queries.size());
            for (const auto& synth_q : decision.leg_synthetic_queries) {
                auto synth_vec = co_await pipeline.embedder().embed_synth(synth_q);
                synth_reqs.push_back({std::move(synth_vec), synth_top_k, 0.0f,
                                      std::nullopt});
            }

            auto synth_batches = co_await leg_store->batch_search(std::move(synth_reqs));
            for (auto& synth_raw : synth_batches) {
                for (auto& h : synth_raw) {
                    auto cit = h.payload.find("case_id");
                    const std::string& cid = (cit != h.payload.end()) ? cit->second : h.id;
                    if (forced_set.count(cid) && !seen_inject.count(cid)) {
                        erase_by_id(raw, h.id);
                        seen_inject.insert(cid);
                        injected_ids.push_back(h.id); // UUID for CE gate
                        injections.push_back(std::move(h));
                    }
                }
            }
        }

        // Fetch any forced section not yet seen via synth search.
        // Qdrant fetch() requires UUIDs; forced section IDs are case_id strings
        // (e.g. "NZLEG/RTA/s19"). Use a payload filter search instead.
        // Batch these too - they're rare (synth search usually finds them).
        //
        // Fetch limit=8 per section instead of 1 so we can pick the chunk with
        // the lowest chunk_index that is not an amendment/transitional note.
        // Background: the nz_legal collection has two corruption patterns -
        //   (a) long sections split into chunk_index=0..N windows; limit=1 may
        //       return a mid-section fragment rather than the opening.
        //   (b) historical or amendment act provisions share a case_id with the
        //       current operative section; these begin with known amendment phrases.
        {
            // Returns true when a payload text begins with amendment/transitional language.
            auto is_amendment_chunk = [](const QdrantPoint& pt) -> bool {
                auto it = pt.payload.find("text");
                if (it == pt.payload.end()) return false;
                const std::string& text = it->second;
                const size_t scan = std::min(text.size(), size_t(250));
                std::string head(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(scan));
                std::transform(head.begin(), head.end(), head.begin(), ::tolower);
                static const std::vector<std::string> AMEND_PATS = {
                    "amendment made by section",
                    "amendments made by section",   // plural form e.g. s51
                    "as inserted by section",
                    "as amended by section",
                    "does not apply to an increase",
                    "does not apply to any",
                    "does not apply whether",
                };
                for (const auto& pat : AMEND_PATS) {
                    if (head.find(pat) != std::string::npos) return true;
                }
                return false;
            };

            auto chunk_index_of = [](const QdrantPoint& pt) -> int {
                auto it = pt.payload.find("chunk_index");
                if (it == pt.payload.end()) return INT_MAX; // never preferred; bad data sorts last
                try { return std::stoi(it->second); } catch (...) {
                    SPDLOG_WARN("chunk_index_of: non-int chunk_index='{}' for pt {}",
                                it->second, pt.id);
                    return INT_MAX;
                }
            };

            std::vector<BatchSearchRequest> miss_reqs;
            std::vector<std::string>        miss_sids;
            for (const auto& sid : decision.forced_sections) {
                if (seen_inject.count(sid)) continue;
                QdrantFilter case_filt;
                case_filt.must.push_back({"case_id", {sid}});
                miss_reqs.push_back({query_vector, 64, 0.0f, std::move(case_filt)});
                miss_sids.push_back(sid);
            }
            if (!miss_reqs.empty()) {
                auto miss_batches = co_await leg_store->batch_search(std::move(miss_reqs));
                for (size_t i = 0; i < miss_batches.size(); ++i) {
                    auto& cands = miss_batches[i];
                    if (cands.empty()) continue;
                    const std::string& sid = miss_sids[i];

                    // Prefer the operative chunk with the lowest chunk_index.
                    //
                    // Selection order:
                    // 1. Group by title.
                    // 2. Discard groups with transitional/amendment title language.
                    // 3. Among remaining groups, pick the one with the most entries
                    //    (longer operative section = more chunks; wrong historical
                    //    sections tend to be a single short chunk).
                    // 4. Within the winning group, skip amendment text chunks.
                    // 5. Return the chunk with the lowest chunk_index.
                    //
                    // This handles two corpus corruption patterns:
                    //  (a) case_id collision: e.g. NZLEG/RTA/s40 contains both
                    //      "Remuneration of Principal Tenancy Adjudicator" (old)
                    //      and "Tenant's responsibilities" (current, 4+ chunks).
                    //  (b) mid-section fragments: long sections split into windows;
                    //      limit=1 used to return a random window.
                    std::unordered_map<std::string, std::vector<QdrantPoint*>> by_title;
                    for (auto& c : cands) {
                        auto it = c.payload.find("title");
                        const std::string& t = (it != c.payload.end()) ? it->second : "";
                        by_title[t].push_back(&c);
                    }

                    // Reject title groups that look transitional/non-operative.
                    static const std::vector<std::string> TRANS_WORDS = {
                        "application of", "savings", "transitional", "commencement",
                        "repeal", "covid", "inserted by",
                    };
                    auto has_trans_title = [&](const std::string& title) -> bool {
                        std::string low = title;
                        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                        for (const auto& w : TRANS_WORDS)
                            if (low.find(w) != std::string::npos) return true;
                        return false;
                    };

                    std::vector<std::pair<std::string, std::vector<QdrantPoint*>>> groups;
                    for (auto& [t, pts] : by_title)
                        if (!has_trans_title(t)) groups.emplace_back(t, pts);
                    if (groups.empty())
                        for (auto& [t, pts] : by_title) groups.emplace_back(t, pts);

                    // Pick the group with the most chunks (most likely current operative).
                    auto& best_group = *std::max_element(
                        groups.begin(), groups.end(),
                        [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });
                    auto& group_ptrs = best_group.second;

                    // Within the group, skip amendment-text chunks.
                    std::vector<QdrantPoint*> operative;
                    for (auto* p : group_ptrs)
                        if (!is_amendment_chunk(*p)) operative.push_back(p);
                    if (operative.empty()) operative = group_ptrs;

                    auto* best = *std::min_element(
                        operative.begin(), operative.end(),
                        [&](const QdrantPoint* a, const QdrantPoint* b) {
                            return chunk_index_of(*a) < chunk_index_of(*b);
                        });

                    erase_by_id(raw, best->id);
                    seen_inject.insert(sid);
                    injected_ids.push_back(best->id); // UUID for CE gate
                    injections.push_back(std::move(*best));
                }
            }
        }

        injections.insert(injections.end(),
                          std::make_move_iterator(raw.begin()),
                          std::make_move_iterator(raw.end()));
        raw = std::move(injections);

        // Structural filters.
        const std::string combined_q = normalize_query(
            original_question.empty() ? question : (original_question + " " + question));
        const auto& lp = jurisdiction.low_priority_sections();

        // pt.id is a Qdrant UUID; case_id in the payload is the human-readable
        // section identifier (e.g. "NZLEG/RTA/s16A"). Use payload for all checks.
        auto get_case_id = [](const QdrantPoint& pt) -> const std::string& {
            auto it = pt.payload.find("case_id");
            return it != pt.payload.end() ? it->second : pt.id;
        };

        raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const QdrantPoint& pt) {
            return !allow_section(get_case_id(pt), combined_q, lp);
        }), raw.end());

        if (!decision.leg_allow_list.empty()) {
            const std::unordered_set<std::string> allow_set(
                decision.leg_allow_list.begin(), decision.leg_allow_list.end());
            // Forced sections from any matched route are never stripped by the allow_list.
            // The allow_list restricts vector-search noise only.
            const std::unordered_set<std::string> injected_set(
                injected_ids.begin(), injected_ids.end());
            raw.erase(std::remove_if(raw.begin(), raw.end(), [&](const QdrantPoint& pt) {
                const std::string& cid = get_case_id(pt);
                return detail::is_leg_chunk(cid) && !allow_set.count(cid)
                       && !injected_set.count(pt.id);
            }), raw.end());
        }

        // Keep only legislation chunks (prevents case decisions leaking in).
        // case_id is in the payload (e.g. "NZLEG/RTA/s19"); pt.id is a UUID.
        raw.erase(std::remove_if(raw.begin(), raw.end(), [](const QdrantPoint& pt) {
            auto it = pt.payload.find("case_id");
            return it == pt.payload.end() || !detail::is_leg_chunk(it->second);
        }), raw.end());

        // Cross-encoder relevance gate.
        if (pipeline.reranker().enabled() && !raw.empty()) {
            const std::unordered_set<std::string> inj_set(
                injected_ids.begin(), injected_ids.end());
            std::vector<RerankCandidate> candidates;
            candidates.reserve(raw.size());
            for (const auto& pt : raw) {
                RerankCandidate rc;
                rc.id    = pt.id;
                rc.text  = detail::payload_field(pt, "text");
                rc.score = pt.score;
                rc.forced = inj_set.count(pt.id) > 0;
                candidates.push_back(std::move(rc));
            }

            try {
                auto ranked = co_await pipeline.reranker().score_and_filter(
                    question, std::move(candidates), jurisdiction.leg_ce_min_score());

                std::unordered_map<std::string, QdrantPoint> pt_map;
                for (auto& pt : raw)
                    pt_map.emplace(pt.id, std::move(pt));

                raw.clear();
                for (const auto& rc : ranked) {
                    auto it = pt_map.find(rc.id);
                    if (it != pt_map.end())
                        raw.push_back(std::move(it->second));
                }
            } catch (const std::exception& e) {
                SPDLOG_WARN("retrieve_anchor: CE gate failed: {}", e.what());
                // Proceed with unfiltered candidates.
            }
        }

        // Deduplicate and cap.
        const int n_forced = static_cast<int>(injected_ids.size());
        const int max_hits = detail::compute_max_hits(n_forced, decision.leg_allow_list.size());
        // Invariant 1: an unrouted query must never silently adopt the cap shape of a
        // routed query. If this fires, compute_max_hits or the forced-section injection
        // path has regressed.
        assert(decision.triggered || max_hits == 2);
        // Invariant 2: the cap must be at least as large as the number of forced sections
        // we actually injected. If this fires, a route has leg_allow_list.size() <
        // forced_sections.size() (authoring slip) — the cap would silently truncate
        // forced sections that the route declared mandatory.
        assert(n_forced <= max_hits);
        std::unordered_set<std::string> seen;
        std::vector<QdrantPoint> hits;
        for (auto& pt : raw) {
            if (seen.insert(pt.id).second) {
                hits.push_back(std::move(pt));
                if (static_cast<int>(hits.size()) >= max_hits) break;
            }
        }

        if (hits.empty()) {
            AnchorResult r;
            r.elapsed_ms    = elapsed();
            r.route_matched = decision.triggered;
            r.max_hits      = max_hits;
            co_return r;
        }

        // Build anchor text.
        std::string anchor =
            "Relevant Act sections (RTA 1986 live text). "
            "Only reference a section number (e.g. 's28') when ALL THREE hold: "
            "(1) that section appears below, "
            "(2) your specific claim is directly supported by its text, "
            "(3) the section is relevant to the user's issue. "
            "If no section satisfies all three, give practical advice from the "
            "tribunal decisions without any section reference. "
            "Do not use [SN] notation for legislation:";
        std::vector<QdrantPoint> leg_srcs_out;
        leg_srcs_out.reserve(hits.size());
        for (const auto& h : hits) {
            const std::string title = detail::payload_field(h, "title");
            const std::string text  = detail::payload_field(h, "text");
            anchor += "\n\n" + title + "\n" +
                      (text.size() > 1500 ? text.substr(0, 1500) : text);
            leg_srcs_out.push_back(h);
        }

        if (!decision.rule_cards.empty()) {
            anchor += "\n\nRETRIEVED RULE CARD:";
            for (const auto& card : decision.rule_cards)
                anchor += "\n" + card;
        }

        co_return AnchorResult{std::move(anchor), std::move(leg_srcs_out), elapsed(),
                               decision.triggered, max_hits};

    } catch (const std::exception& e) {
        SPDLOG_WARN("retrieve_anchor: {}", e.what());
        AnchorResult r;
        r.elapsed_ms = elapsed();
        co_return r;
    }
    // Non-std exceptions (incl. sanitiser traps) propagate to the caller.
}

// ---------------------------------------------------------------------------
// augment_case_retrieval
// ---------------------------------------------------------------------------

drogon::Task<> augment_case_retrieval(
    std::string question,
    std::string retrieval_question,
    RAGPipeline& pipeline,
    const RouteTable& table,
    std::vector<std::string>& context_texts,
    std::vector<QdrantPoint>& sources,
    const RouteDecision* precomputed)
{
    RouteDecision local_decision;
    const RouteDecision& decision = precomputed
        ? *precomputed
        : (local_decision = build_route_decision(question, retrieval_question, table));
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
    const JurisdictionBase& jurisdiction,
    const RouteTable& table,
    const RouteDecision* precomputed)
{
    try {
        RouteDecision local_decision;
        const RouteDecision& decision = precomputed
            ? *precomputed
            : (local_decision = build_route_decision(
                  original_question.empty() ? question : original_question,
                  question,
                  table));

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

        // court=MANUAL is a distinct field from court_name (the VectorStore
        // constructor filter). court_name is per-source jurisdiction tagging;
        // court="MANUAL" marks hand-curated guidance documents.
        QdrantFilter filt;
        filt.must.push_back({"court", {"MANUAL"}});
        filt.must.push_back({"source_type", GUIDANCE_SOURCE_TYPES});
        auto hits = co_await pipeline.store().search(query_vec, 10, 0.0f, filt);

        // Store pointers into `hits` - moving elements out would leave the
        // fallback vector-search loop below iterating moved-from objects
        // (empty id/payload, silently discarding valid unforced guidance).
        std::unordered_map<std::string, const QdrantPoint*> hits_by_id;
        for (const auto& h : hits)
            hits_by_id.emplace(h.id, &h);

        if (!forced_ids.empty()) {
            const QdrantPoint* best_h = nullptr;
            float best_score = -1.0f;
            for (const auto& gid : forced_ids) {
                if (existing_source_ids.count(gid)) continue;
                auto it = hits_by_id.find(gid);
                if (it != hits_by_id.end() && it->second->score > best_score) {
                    best_h = it->second;
                    best_score = it->second->score;
                }
            }
            if (best_h) {
                co_return GuidanceResult{
                    detail::payload_field(*best_h, "text"),
                    *best_h,
                    "route_forced_vector",
                };
            }

            for (const auto& gid : forced_ids) {
                if (existing_source_ids.count(gid)) continue;
                auto fetched = co_await pipeline.store().fetch({gid});
                if (!fetched.empty()) {
                    co_return GuidanceResult{
                        detail::payload_field(fetched[0], "text"),
                        fetched[0],
                        "route_forced",
                    };
                }
            }
        }

        for (const auto& h : hits) {
            if (h.score < GUIDANCE_THRESHOLD) break;
            if (existing_source_ids.count(h.id)) continue;
            co_return GuidanceResult{detail::payload_field(h, "text"), h, "vector_search"};
        }

        co_return GuidanceResult{};

    } catch (const std::exception& e) {
        SPDLOG_WARN("retrieve_manual_guidance: {}", e.what());
        co_return GuidanceResult{};
    }
    // Non-std exceptions propagate to the caller.
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

    // Deduplicate queries using normalize_query (same normaliser as route matching
    // - handles smart quotes, em-dash transliteration, etc.).
    std::vector<std::string> queries;
    {
        std::unordered_set<std::string> seen;
        for (const auto& q : {original_question, rewritten_question}) {
            if (seen.insert(normalize_query(q)).second)
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
