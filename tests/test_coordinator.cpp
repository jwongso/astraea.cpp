#include "astraea/in_process_coordinator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <optional>
#include <utility>

namespace {

// Same minimal Coro test harness as test_async_semaphore.cpp - drives a
// coroutine synchronously to its first suspension. Untimed acquire on a
// permit-available semaphore takes the fast path and completes inline.
struct Coro {
    struct promise_type {
        Coro get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};

Coro acquire_untimed(astraea::InProcessCoordinator& c,
                     std::optional<astraea::CoordinatorClient::Permit>& slot,
                     bool& done)
{
    slot.emplace(co_await c.acquire());
    done = true;
}

Coro acquire_timed(astraea::InProcessCoordinator& c,
                   std::chrono::milliseconds timeout,
                   std::optional<std::optional<astraea::CoordinatorClient::Permit>>& slot,
                   bool& done)
{
    slot.emplace(co_await c.acquire(timeout));
    done = true;
}

} // namespace

TEST_CASE("InProcessCoordinator: identifies as in_process with correct max",
          "[coordinator][in_process]") {
    astraea::InProcessCoordinator c(4);
    REQUIRE(std::string(c.backend_name()) == "in_process");
    REQUIRE(c.max_concurrency() == 4);
    REQUIRE(c.available() == 4);
    REQUIRE(c.waiters()   == 0);
}

TEST_CASE("InProcessCoordinator: untimed acquire delegates and releases through interface",
          "[coordinator][in_process]") {
    astraea::InProcessCoordinator c(1);
    std::optional<astraea::CoordinatorClient::Permit> p;
    bool d = false;
    acquire_untimed(c, p, d);

    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE(p->held());
    REQUIRE(c.available() == 0);

    // Release via the interface's RAII Permit -> virtual dispatch into
    // InProcessPermit's destructor -> AsyncSemaphore::Permit destructor.
    p.reset();
    REQUIRE(c.available() == 1);
}

TEST_CASE("InProcessCoordinator: timed acquire fast-path returns Permit",
          "[coordinator][in_process]") {
    astraea::InProcessCoordinator c(1);
    std::optional<std::optional<astraea::CoordinatorClient::Permit>> p;
    bool d = false;
    acquire_timed(c, std::chrono::milliseconds(0), p, d);

    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE(p->has_value());
    REQUIRE((*p)->held());
    REQUIRE(c.available() == 0);
}

TEST_CASE("InProcessCoordinator: timed acquire returns nullopt on contention",
          "[coordinator][in_process]") {
    astraea::InProcessCoordinator c(1);
    std::optional<astraea::CoordinatorClient::Permit> first;
    bool d_first = false;
    acquire_untimed(c, first, d_first);
    REQUIRE(d_first);
    REQUIRE(c.available() == 0);

    std::optional<std::optional<astraea::CoordinatorClient::Permit>> p;
    bool d = false;
    acquire_timed(c, std::chrono::milliseconds(0), p, d);

    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE_FALSE(p->has_value());
    REQUIRE(c.waiters() == 0); // timeout=0 must NOT enqueue
}

TEST_CASE("InProcessCoordinator: Permit move through interface releases exactly once",
          "[coordinator][in_process]") {
    astraea::InProcessCoordinator c(1);
    std::optional<astraea::CoordinatorClient::Permit> src;
    bool d = false;
    acquire_untimed(c, src, d);
    REQUIRE(d);
    REQUIRE(c.available() == 0);

    astraea::CoordinatorClient::Permit dest(std::move(*src));
    REQUIRE(dest.held());
    REQUIRE_FALSE(src->held());
    src.reset(); // empty Permit destruction must be a no-op
    REQUIRE(c.available() == 0);

    dest.reset();
    REQUIRE(c.available() == 1);
}
