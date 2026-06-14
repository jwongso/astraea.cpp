// Pure-CPU tests for SessionStore static helpers. No Redis required - the
// helpers under test (truncate_assistant_messages, valid_session_id) are
// network-free. Live in test_session_helpers.cpp rather than next to the
// existing redis_coordinator tests so the parity build (clients=ON but no
// REDIS_HOST) still exercises them.
//
// Coverage:
//   - assistant messages over the cap are truncated, equal/under are left
//     alone, multi-turn vectors handle bursts of long answers
//   - user messages are *never* truncated (the current question is in
//     assembled.messages; only past answers bloat the prompt)
//   - answer_cap <= 0 is a no-op (so operators can disable via env)
//   - valid_session_id rejects empty / too long / non-alnum characters

#include "astraea/session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using astraea::ChatMessage;
using astraea::SessionStore;

TEST_CASE("truncate_assistant_messages caps assistant content", "[session]") {
    std::vector<ChatMessage> turns{
        {"user",      std::string(800, 'q')},
        {"assistant", std::string(800, 'a')},
        {"user",      "short user"},
        {"assistant", "short assistant"},
    };

    SessionStore::truncate_assistant_messages(turns, 400);

    // Long assistant truncated to 400 chars.
    REQUIRE(turns[1].content.size() == 400);
    REQUIRE(turns[1].content == std::string(400, 'a'));
    // Short assistant untouched.
    REQUIRE(turns[3].content == "short assistant");
    // Both user messages untouched regardless of length.
    REQUIRE(turns[0].content.size() == 800);
    REQUIRE(turns[0].content == std::string(800, 'q'));
    REQUIRE(turns[2].content == "short user");
}

TEST_CASE("truncate_assistant_messages with cap=0 is a no-op", "[session]") {
    std::vector<ChatMessage> turns{
        {"assistant", std::string(10000, 'x')},
    };
    SessionStore::truncate_assistant_messages(turns, 0);
    REQUIRE(turns[0].content.size() == 10000);

    SessionStore::truncate_assistant_messages(turns, -1);
    REQUIRE(turns[0].content.size() == 10000);
}

TEST_CASE("truncate_assistant_messages handles empty input", "[session]") {
    std::vector<ChatMessage> turns;
    SessionStore::truncate_assistant_messages(turns, 400);
    REQUIRE(turns.empty());
}

TEST_CASE("valid_session_id accepts hex UUIDs and rejects junk", "[session]") {
    REQUIRE(SessionStore::valid_session_id("abc123"));
    REQUIRE(SessionStore::valid_session_id("11111111-2222-3333-4444-555555555555"));
    REQUIRE(SessionStore::valid_session_id("with_underscore"));

    REQUIRE_FALSE(SessionStore::valid_session_id(""));
    REQUIRE_FALSE(SessionStore::valid_session_id(std::string(65, 'a')));
    REQUIRE_FALSE(SessionStore::valid_session_id("has space"));
    REQUIRE_FALSE(SessionStore::valid_session_id("colon:bad"));
    REQUIRE_FALSE(SessionStore::valid_session_id("slash/bad"));
}
