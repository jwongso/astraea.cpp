#pragma once
// Boundary-aware term matching primitives shared by the routing engine
// (AhoCorasick) and the low-priority section gate (allow_section /
// compute_suppressed_lp_ids).
//
// Single source of truth for "what is a word boundary". All inputs are
// expected to be the output of normalize_query(): lowercase ASCII letters,
// digits, spaces, and possibly raw UTF-8 continuation bytes that survive
// normalisation.
//
// ASCII-only by design. Bytes >= 0x80 (UTF-8 continuation bytes) return
// false from is_route_word_char and are therefore treated as boundaries —
// same gap as the Python _is_route_word_char(). Documented and accepted;
// revisit only if a future route term needs to match across a diacritic.
//
// Do NOT replace with std::isalnum: it is locale-sensitive and has
// signed-char UB when called with a plain char argument.
#include <cstddef>
#include <string_view>

namespace astraea {

[[nodiscard]] inline constexpr bool is_route_word_char(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// True iff the half-open interval [start, end) in `text` sits at word
// boundaries (i.e. both neighbours are absent or non-word).
[[nodiscard]] inline constexpr bool has_route_boundaries(
        std::string_view text, std::size_t start, std::size_t end) noexcept {
    const bool left_ok  = (start == 0)           || !is_route_word_char(static_cast<unsigned char>(text[start - 1]));
    const bool right_ok = (end   == text.size()) || !is_route_word_char(static_cast<unsigned char>(text[end]));
    return left_ok && right_ok;
}

// True iff `needle` occurs as a token in `haystack` — i.e. at least one
// occurrence sits at word boundaries.
//
// Empty needle returns false: there is no sensible "token" for an empty
// term, and accepting it on every position has bitten authoring slips in
// the past. This matches the practical contract of AhoCorasick::_insert,
// which rejects empty terms at insertion time.
//
// Size short-circuit (`needle.size() > haystack.size()`) avoids the find()
// call for the common case of a long term against a short query.
[[nodiscard]] inline bool contains_route_token(
        std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > haystack.size()) return false;
    for (std::size_t pos = 0;
         (pos = haystack.find(needle, pos)) != std::string_view::npos;
         ++pos) {
        if (has_route_boundaries(haystack, pos, pos + needle.size())) return true;
    }
    return false;
}

} // namespace astraea
