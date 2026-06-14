#include "astraea/request_id.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

TEST_CASE("valid_request_id: empty / oversize / bad chars rejected",
          "[request_id]")
{
    REQUIRE_FALSE(astraea::valid_request_id(""));
    REQUIRE_FALSE(astraea::valid_request_id(std::string(65, 'a')));
    REQUIRE_FALSE(astraea::valid_request_id("has spaces"));
    REQUIRE_FALSE(astraea::valid_request_id("has/slash"));
    REQUIRE_FALSE(astraea::valid_request_id("has\nnewline"));
    REQUIRE_FALSE(astraea::valid_request_id("has;semicolon"));
}

TEST_CASE("valid_request_id: alnum + dash + underscore accepted",
          "[request_id]")
{
    REQUIRE(astraea::valid_request_id("a"));
    REQUIRE(astraea::valid_request_id(std::string(64, 'a')));
    REQUIRE(astraea::valid_request_id("550e8400-e29b-41d4-a716-446655440000")); // uuid4
    REQUIRE(astraea::valid_request_id("trace_id_42"));
    REQUIRE(astraea::valid_request_id("UPPER_lower-123"));
}

TEST_CASE("resolve_request_id: passes through valid inbound", "[request_id]") {
    const std::string inbound = "client-supplied-trace-id-42";
    REQUIRE(astraea::resolve_request_id(inbound) == inbound);
}

TEST_CASE("resolve_request_id: generates uuid on missing/invalid", "[request_id]") {
    const std::string r1 = astraea::resolve_request_id("");
    const std::string r2 = astraea::resolve_request_id("has space");
    REQUIRE(astraea::valid_request_id(r1));
    REQUIRE(astraea::valid_request_id(r2));
    REQUIRE(r1 != r2); // two generated IDs should be different
}

TEST_CASE("resolve_request_id: many generated IDs are unique", "[request_id]") {
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        auto id = astraea::resolve_request_id("");
        REQUIRE(astraea::valid_request_id(id));
        REQUIRE(seen.insert(id).second); // collision = test failure
    }
}
