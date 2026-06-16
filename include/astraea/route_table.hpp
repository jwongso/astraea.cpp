#pragma once
#include "astraea/aho_corasick.hpp"
#include "astraea/routing.hpp"
#include <span>

namespace astraea {

/// @brief Pre-built Aho-Corasick automaton over a jurisdiction's route table.
///
/// Construct once per jurisdiction at startup; pass by const reference to
/// build_route_decision() on every request to reuse the automaton instead of
/// rebuilding it per call. Building the AC over a typical jurisdiction
/// (~1400 terms across ~20 routes) is not catastrophic, but it adds up at
/// non-trivial QPS.
///
/// Lifetime: holds a non-owning std::span into the route storage. The
/// underlying route data (typically a static vector inside a jurisdiction TU)
/// MUST outlive the RouteTable. The AC nodes also reference route string
/// storage via string_view; the same invariant applies.
///
/// Non-copyable (the AC trie is large); movable for factory patterns.
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

/// @brief Compute the routing decision using the cached automaton from `table`.
///
/// Equivalent to the span-based build_route_decision() in routing.hpp but
/// reuses the pre-built AhoCorasick from `table` instead of rebuilding it.
/// Prefer this overload on every per-request code path.
RouteDecision build_route_decision(
    std::string_view  original,
    std::string_view  rewritten,
    const RouteTable& table);

} // namespace astraea
