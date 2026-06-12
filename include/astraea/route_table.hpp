#pragma once
#include "astraea/aho_corasick.hpp"
#include "astraea/routing.hpp"
#include <span>

namespace astraea {

// Owns a pre-built Aho-Corasick automaton over a route table.
//
// Construct once per Jurisdiction at startup; pass to build_route_decision()
// per request to skip the per-call AC build. The AC build over a typical
// jurisdiction (~1 400 terms across 20 routes) is roughly O(terms x alphabet)
// trie construction + BFS link fill; not catastrophic per call but it adds
// up at any non-trivial QPS.
//
// Lifetime: holds a non-owning std::span into the route storage. The route
// data (typically a static std::vector<StatuteRoute> inside a jurisdiction
// translation unit) MUST outlive the RouteTable. AC internals also reference
// route string storage via std::string_view, so the same invariant applies.
//
// Non-copyable (the AC is large); movable so the table can be returned from
// a factory or stored in a unique_ptr-less Jurisdiction.
class RouteTable {
public:
    explicit RouteTable(std::span<const StatuteRoute> routes)
        : _routes(routes), _ac(routes) {}

    [[nodiscard]] std::span<const StatuteRoute> routes() const noexcept { return _routes; }
    [[nodiscard]] const AhoCorasick&            ac()     const noexcept { return _ac; }

    RouteTable(const RouteTable&)            = delete;
    RouteTable& operator=(const RouteTable&) = delete;
    RouteTable(RouteTable&&)                 = default;
    RouteTable& operator=(RouteTable&&)      = default;

private:
    std::span<const StatuteRoute> _routes;
    AhoCorasick                   _ac;
};

// Same semantics as the span-based overload in routing.hpp, but reuses the
// cached automaton from `table` instead of rebuilding it. Prefer this on
// every per-request code path.
RouteDecision build_route_decision(
    std::string_view  original,
    std::string_view  rewritten,
    const RouteTable& table);

} // namespace astraea
