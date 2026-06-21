#pragma once
#include "astraea/routing.hpp"
#include "astraea/term_match.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace astraea {

/// @brief Identifies which field list within a StatuteRoute a matched pattern came from.
enum class AcField : uint8_t {
    Exclude, ///< From StatuteRoute::exclude_any.
    Precise, ///< From StatuteRoute::include_any_precise.
    Broad,   ///< From StatuteRoute::include_any_broad.
    Context, ///< From StatuteRoute::require_context_any.
    Legacy,  ///< From StatuteRoute::include_any (legacy flat list).
    All,     ///< Synthetic value used for internal matching logic.
};

/// @brief A single pattern match returned by AhoCorasick::search().
struct AcHit {
    int              route_idx; ///< Index into the StatuteRoute span passed to the constructor.
    AcField          field; ///< Which list the matched pattern came from.
    std::string_view term; ///< View into the route's string storage (stable for process lifetime).
};

/// @brief Aho-Corasick multi-pattern automaton over all route terms for one jurisdiction.
///
/// Build once per jurisdiction at startup; search() runs in O(|text| + |matches|).
/// All route terms must be ASCII lower-case (they are for every current jurisdiction).
/// The automaton nodes store string_view references into the route storage; the
/// route data must outlive the AhoCorasick instance.
class AhoCorasick {
public:
    AhoCorasick() = default;
    explicit AhoCorasick(std::span<const StatuteRoute> routes);

    /// @brief Find all pattern occurrences in `text` and return them as AcHit records.
    [[nodiscard]] std::vector<AcHit> search(std::string_view text) const;
    [[nodiscard]] bool empty() const noexcept { return _nodes.empty(); } ///< True when no patterns were inserted (empty route table).
    [[nodiscard]] std::size_t node_count() const noexcept { return _nodes.size(); } ///< Number of trie nodes; useful for size introspection in tests.

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
