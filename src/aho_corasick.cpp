#include "astraea/aho_corasick.hpp"
#include <deque>

namespace astraea {

int AhoCorasick::_new_node() {
    _nodes.emplace_back();
    return static_cast<int>(_nodes.size()) - 1;
}

void AhoCorasick::_insert(std::string_view term, int route_idx, AcField field) {
    int cur = 0;
    for (unsigned char c : term) {
        if (c >= 128) return; // skip non-ASCII terms (none exist in current jurisdictions)
        auto& nxt = _nodes[cur].next[c];
        if (nxt == -1) nxt = _new_node();
        cur = nxt;
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

std::vector<AcHit> AhoCorasick::search(std::string_view text) const {
    if (_nodes.empty()) return {};
    std::vector<AcHit> hits;
    int cur = 0;
    for (unsigned char c : text) {
        if (c >= 128) { cur = 0; continue; } // non-ASCII resets to root (rare post-normalize)
        cur = _nodes[cur].next[c];            // always valid after _build_links
        for (const auto& h : _nodes[cur].out)
            hits.push_back(h);
    }
    return hits;
}

} // namespace astraea
