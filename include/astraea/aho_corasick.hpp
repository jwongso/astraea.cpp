#pragma once
#include "astraea/routing.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astraea {

// Which list within a StatuteRoute a pattern came from.
enum class AcField : uint8_t {
    Exclude, Precise, Broad, Context, Legacy, All,
};

struct AcHit {
    int              route_idx;
    AcField          field;
    std::string_view term;  // view into the route's string storage (stable for process lifetime)
};

// Aho-Corasick automaton over all route terms in one jurisdiction.
//
// Build once per Jurisdiction at startup; search() is O(|text| + |matches|).
// All route terms must be ASCII (they are for every current jurisdiction).
//
// Usage:
//   AhoCorasick ac(jur.routes());
//   auto hits = ac.search(normalize_query(question));
class AhoCorasick {
public:
    AhoCorasick() = default;
    explicit AhoCorasick(std::span<const StatuteRoute> routes);

    [[nodiscard]] std::vector<AcHit> search(std::string_view text) const;
    [[nodiscard]] bool empty() const noexcept { return _nodes.empty(); }
    [[nodiscard]] std::size_t node_count() const noexcept { return _nodes.size(); }

private:
    struct Node {
        std::array<int, 128> next;  // goto function; -1 until filled, then always valid
        int                  fail = 0;
        std::vector<AcHit>   out;   // patterns ending here (includes dict-link outputs)

        Node() { next.fill(-1); }
    };

    std::vector<Node> _nodes;

    int  _new_node();
    void _insert(std::string_view term, int route_idx, AcField field);
    void _build_links();
};

} // namespace astraea
