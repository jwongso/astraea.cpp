#include "astraea/async_semaphore.hpp"

#include <catch2/catch_test_macros.hpp>

#include <coroutine>
#include <optional>

namespace {

// Minimal coroutine type for unit-testing AsyncSemaphore without dragging in
// Drogon's event loop. initial_suspend / final_suspend are suspend_never so
// the coroutine body executes synchronously up to the first co_await; any
// subsequent suspension is driven by AsyncSemaphore::release() resuming the
// captured handle on the current thread (no loop attached -> direct resume).
struct Coro {
    struct promise_type {
        Coro get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};

Coro acquire_into(astraea::AsyncSemaphore& sem,
                  std::optional<astraea::AsyncSemaphore::Permit>& slot,
                  bool& done)
{
    slot.emplace(co_await sem.acquire());
    done = true;
}

} // namespace

TEST_CASE("AsyncSemaphore: initial state matches permits", "[async_semaphore]") {
    astraea::AsyncSemaphore sem(3);
    REQUIRE(sem.available() == 3);
    REQUIRE(sem.waiters()   == 0);
}

TEST_CASE("AsyncSemaphore: fast-path acquire decrements available", "[async_semaphore]") {
    astraea::AsyncSemaphore sem(2);
    std::optional<astraea::AsyncSemaphore::Permit> p1, p2;
    bool d1 = false, d2 = false;
    acquire_into(sem, p1, d1);
    acquire_into(sem, p2, d2);
    REQUIRE(d1);
    REQUIRE(d2);
    REQUIRE(sem.available() == 0);
    REQUIRE(sem.waiters()   == 0);
    REQUIRE(p1->held());
    REQUIRE(p2->held());
}

TEST_CASE("AsyncSemaphore: waiter queues when at capacity, resumes on release",
          "[async_semaphore]") {
    astraea::AsyncSemaphore sem(1);

    std::optional<astraea::AsyncSemaphore::Permit> p1, p2, p3;
    bool d1 = false, d2 = false, d3 = false;

    acquire_into(sem, p1, d1);
    REQUIRE(d1);
    REQUIRE(sem.available() == 0);

    acquire_into(sem, p2, d2);
    REQUIRE_FALSE(d2);
    REQUIRE(sem.waiters() == 1);

    acquire_into(sem, p3, d3);
    REQUIRE_FALSE(d3);
    REQUIRE(sem.waiters() == 2);

    // Release p1 -> p2 should resume (FIFO), p3 still queued.
    p1.reset();
    REQUIRE(d2);
    REQUIRE_FALSE(d3);
    REQUIRE(sem.waiters() == 1);
    REQUIRE(sem.available() == 0); // permit handed directly to p2

    // Release p2 -> p3 resumes.
    p2.reset();
    REQUIRE(d3);
    REQUIRE(sem.waiters()   == 0);
    REQUIRE(sem.available() == 0);

    // Final release returns the permit to the pool.
    p3.reset();
    REQUIRE(sem.available() == 1);
}

TEST_CASE("AsyncSemaphore: Permit move transfers ownership without double release",
          "[async_semaphore]") {
    astraea::AsyncSemaphore sem(1);
    std::optional<astraea::AsyncSemaphore::Permit> src;
    bool d = false;
    acquire_into(sem, src, d);
    REQUIRE(d);
    REQUIRE(sem.available() == 0);

    astraea::AsyncSemaphore::Permit dest(std::move(*src));
    REQUIRE(dest.held());
    REQUIRE_FALSE(src->held());
    src.reset(); // empty Permit destruction is a no-op
    REQUIRE(sem.available() == 0);

    // Releasing dest returns the permit.
    dest.reset();
    REQUIRE(sem.available() == 1);
}
