// Direct unit tests for the shared boundary primitives in
// include/astraea/term_match.hpp. These pin the contract used by both the
// AhoCorasick routing engine and the low-priority section gate.
#include "astraea/term_match.hpp"
#include <catch2/catch_all.hpp>

using namespace astraea;

TEST_CASE("is_route_word_char: ASCII-only, lowercase letters / digits / underscore",
          "[term_match]") {
    REQUIRE(is_route_word_char('a'));
    REQUIRE(is_route_word_char('z'));
    REQUIRE(is_route_word_char('0'));
    REQUIRE(is_route_word_char('9'));
    REQUIRE(is_route_word_char('_'));
    // Uppercase: explicitly NOT a word char. normalize_query() lowercases
    // before this predicate ever sees the byte.
    REQUIRE_FALSE(is_route_word_char('A'));
    REQUIRE_FALSE(is_route_word_char('Z'));
    REQUIRE_FALSE(is_route_word_char(' '));
    REQUIRE_FALSE(is_route_word_char('-'));
    REQUIRE_FALSE(is_route_word_char('\''));
    // UTF-8 continuation bytes treated as boundaries (documented gap).
    REQUIRE_FALSE(is_route_word_char(0x80));
    REQUIRE_FALSE(is_route_word_char(0xC4));
    REQUIRE_FALSE(is_route_word_char(0xFF));
}

TEST_CASE("has_route_boundaries: half-open [start, end) at edges and middles",
          "[term_match]") {
    const std::string_view t = "tenant repair";
    REQUIRE(has_route_boundaries(t, 0, 6));   // "tenant" left=BOF, right=space
    REQUIRE(has_route_boundaries(t, 7, 13));  // "repair" left=space, right=EOF
    // Inside "tenant" — "ant" sits at [3,6) but right edge is 't' (word char).
    REQUIRE_FALSE(has_route_boundaries(t, 3, 6));
    // Inside "repair" — "pair" sits at [9,13). Left edge is 'e' (word char).
    REQUIRE_FALSE(has_route_boundaries(t, 9, 13));
    // Whole string boundaries (empty pre/post).
    REQUIRE(has_route_boundaries(t, 0, t.size()));
}

TEST_CASE("contains_route_token: pins the regressions PR1+37e4eee fixed",
          "[term_match][regression]") {
    // The full reason this file exists.
    REQUIRE_FALSE(contains_route_token("tenant",       "ant"));
    REQUIRE_FALSE(contains_route_token("the method",   "meth"));
    REQUIRE_FALSE(contains_route_token("methodology",  "meth"));
    REQUIRE_FALSE(contains_route_token("harmless",     "harm"));
    REQUIRE_FALSE(contains_route_token("pharmacy",     "harm"));
    REQUIRE_FALSE(contains_route_token("charm",        "harm"));
    REQUIRE_FALSE(contains_route_token("bonded",       "bond"));
    REQUIRE_FALSE(contains_route_token("unappealing",  "appeal"));
    REQUIRE_FALSE(contains_route_token("reviewing",    "viewing"));
    REQUIRE_FALSE(contains_route_token("alternative",  "alter"));
    REQUIRE_FALSE(contains_route_token("painted",      "paint"));

    // Standalone tokens still match.
    REQUIRE(contains_route_token("an ant infestation", "ant"));
    REQUIRE(contains_route_token("meth contamination", "meth"));
    REQUIRE(contains_route_token("he caused harm",     "harm"));
    REQUIRE(contains_route_token("bond refund",        "bond"));
    REQUIRE(contains_route_token("appeal the order",   "appeal"));
}

TEST_CASE("contains_route_token: multi-occurrence, only one valid match needed",
          "[term_match]") {
    // First occurrence has a left-side word char; second one is a clean token.
    REQUIRE(contains_route_token("repaint and then paint dries", "paint"));
    // All occurrences embedded — no match.
    REQUIRE_FALSE(contains_route_token("repaint painting painted", "paint"));
}

TEST_CASE("contains_route_token: degenerate inputs",
          "[term_match]") {
    REQUIRE_FALSE(contains_route_token("",      ""));
    REQUIRE_FALSE(contains_route_token("hello", ""));
    REQUIRE_FALSE(contains_route_token("",      "hello"));
    // Size short-circuit: needle longer than haystack.
    REQUIRE_FALSE(contains_route_token("hi", "hello"));
    // Exact full-string match: boundaries are BOF + EOF.
    REQUIRE(contains_route_token("hello", "hello"));
}

TEST_CASE("contains_route_token: UTF-8 boundary behaviour (documented gap)",
          "[term_match]") {
    // The route table currently contains the literal "kainga ora" (ASCII).
    // If someone adds the diacritic form "kāinga ora" as a term, bytes >= 0x80
    // are treated as boundaries — so "ora" matches inside "kāinga ora" (the
    // space before "ora" is a boundary). This is the documented gap; the test
    // pins the current contract so a future change is visible.
    const std::string_view text = "k\xC4\x81inga ora";   // "kāinga ora"
    REQUIRE(contains_route_token(text, "ora"));
    REQUIRE(contains_route_token(text, "inga"));
    // A run that crosses a UTF-8 boundary into a continuation byte is treated
    // as boundary-OK on the UTF-8 side: "k" is a word char and the next byte
    // (0xC4) is non-word, so "k" alone matches at the start.
    REQUIRE(contains_route_token(text, "k"));
}
