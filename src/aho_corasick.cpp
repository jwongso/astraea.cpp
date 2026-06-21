#include "astraea/aho_corasick.hpp"
#include <deque>

namespace astraea {

int AhoCorasick::_new_node() {
    _nodes.emplace_back();
    return static_cast<int>(_nodes.size()) - 1;
}

void AhoCorasick::_insert(std::string_view term, int route_idx, AcField field) {
    if (term.empty()) return;  // empty term at the root would hit on every byte position
    // Reject non-ASCII terms whole; partial mid-walk return would leave dead prefix nodes
    // that consume memory but never produce a match.
    for (unsigned char c : term)
        if (c >= 128) return;
    int cur = 0;
    for (unsigned char c : term) {
        // Do NOT hold a reference across _new_node(): emplace_back may reallocate
        // _nodes, invalidating any outstanding reference. Index-based re-access after
        // the call is safe (C++17 sequences RHS before LHS in assignment).
        if (_nodes[cur].next[c] == -1)
            _nodes[cur].next[c] = _new_node();
        cur = _nodes[cur].next[c];
    }
    _nodes[cur].out.push_back({route_idx, field, term});
}

void AhoCorasick::_build_links() {
    std::deque<int> bfs;

    // Depth-1 nodes: fail -> root. Fill root's missing transitions with self-loop.
    for (int c = 0; c < 128; ++c) {
        int v = _nodes[0].next[c];
        if (v == -1) {
            _nodes[0].next[c] = 0;  // root self-loop for unrecognised first char
        } else {
            _nodes[v].fail = 0;
            bfs.push_back(v);
        }
    }

    while (!bfs.empty()) {
        int u = bfs.front(); bfs.pop_front();
        for (int c = 0; c < 128; ++c) {
            int v = _nodes[u].next[c];
            if (v == -1) {
                // Shortcut: reuse fail-chain result so search never has to chase
                _nodes[u].next[c] = _nodes[_nodes[u].fail].next[c];
            } else {
                // v's fail is the compiled goto from u's fail node
                _nodes[v].fail = _nodes[_nodes[u].fail].next[c];
                // Merge the fail node's full output into v (dict-link outputs)
                const auto& fo = _nodes[_nodes[v].fail].out;
                _nodes[v].out.insert(_nodes[v].out.end(), fo.begin(), fo.end());
                bfs.push_back(v);
            }
        }
    }
}

AhoCorasick::AhoCorasick(std::span<const StatuteRoute> routes) {
    if (routes.empty()) return;
    _new_node(); // root = 0

    for (int i = 0; i < static_cast<int>(routes.size()); ++i) {
        const auto& r = routes[i];
        for (const auto& t : r.exclude_any)          _insert(t, i, AcField::Exclude);
        for (const auto& t : r.include_any_precise)  _insert(t, i, AcField::Precise);
        for (const auto& t : r.include_any_broad)    _insert(t, i, AcField::Broad);
        for (const auto& t : r.require_context_any)  _insert(t, i, AcField::Context);
        for (const auto& t : r.include_any)          _insert(t, i, AcField::Legacy);
        for (const auto& t : r.include_all)          _insert(t, i, AcField::All);
    }
    _build_links();
}

// Route-word character predicate — ASCII-only, mirrors _is_route_word_char() in
// core/routing.py. The route table is normalized to lowercase before matching
// (normalize_query lowercases all ASCII letters); uppercase letters cannot reach
// this function and are deliberately omitted, not assumed safe.
//
// Do NOT replace with std::isalnum: it is locale-sensitive and has signed-char
// UB when called with a plain char argument.
//
// Known, documented gap: bytes >= 0x80 (UTF-8 continuation bytes) return false,
// i.e. they are treated as boundaries. This is an accepted limitation. The route
// table contains Maori-language terms ("kainga ora") whose diacritics survive
// normalize_query as raw UTF-8 bytes; if such bytes appear adjacent to a match
// they will be treated as word boundaries rather than word characters. Document
// any future term relying on non-ASCII adjacency explicitly before adding it.
//
// Verified single consumer: search() is called only by build_route_decision_impl
// in routing.cpp (and the legacy one-shot overload). If a second consumer is
// added that requires raw-substring behavior, introduce an AcSearchMode enum
// rather than silently reverting this predicate.
static constexpr bool is_route_word_char(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Half-open [start, end) convention throughout.
static constexpr bool has_route_boundaries(
        std::string_view text, std::size_t start, std::size_t end) noexcept {
    const bool left_ok  = (start == 0)            || !is_route_word_char(static_cast<unsigned char>(text[start - 1]));
    const bool right_ok = (end   == text.size())  || !is_route_word_char(static_cast<unsigned char>(text[end]));
    return left_ok && right_ok;
}

std::vector<AcHit> AhoCorasick::search(std::string_view text) const {
    if (_nodes.empty()) return {};
    std::vector<AcHit> hits;
    int cur = 0;
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 128) { cur = 0; continue; } // non-ASCII resets to root (rare post-normalize)
        cur = _nodes[cur].next[c];            // always valid after _build_links
        for (const auto& h : _nodes[cur].out) {
            // Accept the hit only when the matched token sits at word boundaries.
            // match occupies [start, i+1) in text; i is the inclusive end index.
            const std::size_t start = i + 1 - h.term.size();
            if (has_route_boundaries(text, start, i + 1))
                hits.push_back(h);
        }
    }
    return hits;
}

} // namespace astraea
