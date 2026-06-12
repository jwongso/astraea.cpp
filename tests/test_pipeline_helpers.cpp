#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "astraea/detail/anchor_helpers.hpp"
#include "astraea/detail/pipeline_helpers.hpp"

using namespace astraea;
using namespace astraea::detail;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QdrantPoint make_point(std::string id, float score,
                               std::string court = "",
                               std::string source_type = "",
                               std::string text = "") {
    QdrantPoint pt;
    pt.id    = std::move(id);
    pt.score = score;
    if (!court.empty())       pt.payload["court"]       = std::move(court);
    if (!source_type.empty()) pt.payload["source_type"] = std::move(source_type);
    if (!text.empty())        pt.payload["text"]         = std::move(text);
    return pt;
}

// ---------------------------------------------------------------------------
// apply_manual_discounts
// ---------------------------------------------------------------------------

TEST_CASE("apply_manual_discounts: non-MANUAL court unchanged", "[pipeline_helpers]") {
    auto pts = std::vector<QdrantPoint>{make_point("a", 0.9f, "TT", "case_law")};
    apply_manual_discounts(pts);
    REQUIRE_THAT(pts[0].score, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("apply_manual_discounts: MANUAL law_review gets 0.85x", "[pipeline_helpers]") {
    auto pts = std::vector<QdrantPoint>{make_point("a", 1.0f, "MANUAL", "law_review")};
    apply_manual_discounts(pts);
    REQUIRE_THAT(pts[0].score, WithinAbs(0.85f, 1e-5f));
}

TEST_CASE("apply_manual_discounts: MANUAL commercial_commentary gets 0.80x", "[pipeline_helpers]") {
    auto pts = std::vector<QdrantPoint>{make_point("a", 1.0f, "MANUAL", "commercial_commentary")};
    apply_manual_discounts(pts);
    REQUIRE_THAT(pts[0].score, WithinAbs(0.80f, 1e-5f));
}

TEST_CASE("apply_manual_discounts: MANUAL official_policy unchanged", "[pipeline_helpers]") {
    auto pts = std::vector<QdrantPoint>{make_point("a", 0.9f, "MANUAL", "official_policy")};
    apply_manual_discounts(pts);
    REQUIRE_THAT(pts[0].score, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("apply_manual_discounts: MANUAL unknown source_type unchanged", "[pipeline_helpers]") {
    auto pts = std::vector<QdrantPoint>{make_point("a", 0.9f, "MANUAL", "unknown_type")};
    apply_manual_discounts(pts);
    REQUIRE_THAT(pts[0].score, WithinAbs(0.9f, 1e-5f));
}

// ---------------------------------------------------------------------------
// deduplicate
// ---------------------------------------------------------------------------

TEST_CASE("deduplicate: keeps highest score per id", "[pipeline_helpers]") {
    std::vector<QdrantPoint> pts = {
        make_point("a", 0.9f),
        make_point("b", 0.8f),
        make_point("a", 0.7f), // duplicate, lower score
    };
    auto result = deduplicate(std::move(pts), 10);
    REQUIRE(result.size() == 2);
    // "a" should have score 0.9, not 0.7
    auto it = std::find_if(result.begin(), result.end(),
                           [](const QdrantPoint& p) { return p.id == "a"; });
    REQUIRE(it != result.end());
    REQUIRE_THAT(it->score, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("deduplicate: returns sorted descending", "[pipeline_helpers]") {
    std::vector<QdrantPoint> pts = {
        make_point("b", 0.5f),
        make_point("a", 0.9f),
        make_point("c", 0.7f),
    };
    auto result = deduplicate(std::move(pts), 10);
    REQUIRE(result.size() == 3);
    REQUIRE(result[0].score >= result[1].score);
    REQUIRE(result[1].score >= result[2].score);
}

TEST_CASE("deduplicate: respects top_k cap", "[pipeline_helpers]") {
    std::vector<QdrantPoint> pts = {
        make_point("a", 0.9f),
        make_point("b", 0.8f),
        make_point("c", 0.7f),
    };
    auto result = deduplicate(std::move(pts), 2);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0].id == "a");
    REQUIRE(result[1].id == "b");
}

TEST_CASE("deduplicate: empty input returns empty", "[pipeline_helpers]") {
    auto result = deduplicate({}, 5);
    REQUIRE(result.empty());
}

// ---------------------------------------------------------------------------
// word_set / jaccard
// ---------------------------------------------------------------------------

TEST_CASE("word_set: tokenises and lowercases", "[pipeline_helpers]") {
    auto ws = word_set("The quick BROWN fox");
    REQUIRE(ws.count("the"));
    REQUIRE(ws.count("quick"));
    REQUIRE(ws.count("brown"));
    REQUIRE(ws.count("fox"));
    REQUIRE(ws.size() == 4);
}

TEST_CASE("word_set: empty string returns empty set", "[pipeline_helpers]") {
    REQUIRE(word_set("").empty());
}

TEST_CASE("jaccard: identical sets return 1.0", "[pipeline_helpers]") {
    auto a = word_set("cat sat mat");
    REQUIRE_THAT(jaccard(a, a), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("jaccard: disjoint sets return 0.0", "[pipeline_helpers]") {
    auto a = word_set("cat sat");
    auto b = word_set("dog ran");
    REQUIRE_THAT(jaccard(a, b), WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("jaccard: partial overlap", "[pipeline_helpers]") {
    // a={cat,sat}, b={cat,mat} -> intersection=1, union=3 -> 1/3
    auto a = word_set("cat sat");
    auto b = word_set("cat mat");
    REQUIRE_THAT(jaccard(a, b), WithinAbs(1.0f / 3.0f, 1e-4f));
}

TEST_CASE("jaccard: both empty returns 0.0", "[pipeline_helpers]") {
    REQUIRE_THAT(jaccard({}, {}), WithinAbs(0.0f, 1e-5f));
}

// ---------------------------------------------------------------------------
// mmr_select
// ---------------------------------------------------------------------------

TEST_CASE("mmr_select: returns at most top_k items", "[pipeline_helpers]") {
    std::vector<QdrantPoint> pts = {
        make_point("a", 0.9f, "", "", "alpha beta gamma"),
        make_point("b", 0.8f, "", "", "delta epsilon zeta"),
        make_point("c", 0.7f, "", "", "eta theta iota"),
        make_point("d", 0.6f, "", "", "kappa lambda mu"),
    };
    auto result = mmr_select(pts, 2);
    REQUIRE(result.size() == 2);
}

TEST_CASE("mmr_select: top_k larger than input returns all", "[pipeline_helpers]") {
    std::vector<QdrantPoint> pts = {
        make_point("a", 0.9f),
        make_point("b", 0.8f),
    };
    auto result = mmr_select(pts, 5);
    REQUIRE(result.size() == 2);
}

TEST_CASE("mmr_select: first selected item is highest scored", "[pipeline_helpers]") {
    // Input must be sorted descending by score (as deduplicate() returns).
    std::vector<QdrantPoint> pts = {
        make_point("high", 0.9f, "", "", "completely different words"),
        make_point("low",  0.5f, "", "", "unique terms only here"),
    };
    auto result = mmr_select(pts, 1);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].id == "high");
}

// ---------------------------------------------------------------------------
// is_leg_chunk
// ---------------------------------------------------------------------------

TEST_CASE("is_leg_chunk: prefix contains LEG", "[anchor_helpers]") {
    REQUIRE(is_leg_chunk("NZLEG/001/002"));
    REQUIRE(is_leg_chunk("LEG/001"));
    REQUIRE(is_leg_chunk("NZLEG2/005"));
}

TEST_CASE("is_leg_chunk: case-insensitive", "[anchor_helpers]") {
    REQUIRE(is_leg_chunk("nzleg/001"));
    REQUIRE(is_leg_chunk("Leg/001"));
    REQUIRE(is_leg_chunk("NzLeG/001"));
}

TEST_CASE("is_leg_chunk: non-legislation prefix returns false", "[anchor_helpers]") {
    REQUIRE_FALSE(is_leg_chunk("NZT/001/005"));
    REQUIRE_FALSE(is_leg_chunk("ERA/2024/001"));
    REQUIRE_FALSE(is_leg_chunk("MANUAL/guidance"));
}

TEST_CASE("is_leg_chunk: no slash returns false", "[anchor_helpers]") {
    REQUIRE_FALSE(is_leg_chunk("NZLEG001"));
    REQUIRE_FALSE(is_leg_chunk(""));
}

TEST_CASE("is_leg_chunk: LEG must be in prefix not suffix", "[anchor_helpers]") {
    REQUIRE_FALSE(is_leg_chunk("NZT/LEG001")); // LEG is after the slash
}
