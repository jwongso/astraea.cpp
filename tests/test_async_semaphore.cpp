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

// ---------------------------------------------------------------------------
// Timed acquire (timeout = 0 path is testable without an event loop)
// ---------------------------------------------------------------------------

namespace {

Coro try_acquire_timed(astraea::AsyncSemaphore& sem,
                       std::chrono::milliseconds timeout,
                       std::optional<std::optional<astraea::AsyncSemaphore::Permit>>& slot,
                       bool& done)
{
    slot.emplace(co_await sem.acquire(timeout));
    done = true;
}

} // namespace

TEST_CASE("AsyncSemaphore: acquire(timeout=0) returns Permit when available",
          "[async_semaphore]") {
    astraea::AsyncSemaphore sem(1);
    std::optional<std::optional<astraea::AsyncSemaphore::Permit>> p;
    bool d = false;
    try_acquire_timed(sem, std::chrono::milliseconds(0), p, d);
    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE(p->has_value());          // got a Permit
    REQUIRE((*p)->held());
    REQUIRE(sem.available() == 0);
}

TEST_CASE("AsyncSemaphore: acquire(timeout=0) returns nullopt on contention",
          "[async_semaphore]") {
    astraea::AsyncSemaphore sem(1);
    std::optional<astraea::AsyncSemaphore::Permit> first;
    bool d_first = false;
    acquire_into(sem, first, d_first);
    REQUIRE(d_first);
    REQUIRE(sem.available() == 0);

    // No permit available, timeout = 0 -> immediate nullopt, no enqueue.
    std::optional<std::optional<astraea::AsyncSemaphore::Permit>> p;
    bool d = false;
    try_acquire_timed(sem, std::chrono::milliseconds(0), p, d);
    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE_FALSE(p->has_value());    // nullopt = timed out
    REQUIRE(sem.waiters() == 0);       // crucially: NOT enqueued

    // Releasing the first permit puts it back in the pool, no waiter to satisfy.
    first.reset();
    REQUIRE(sem.available() == 1);
}

TEST_CASE("AsyncSemaphore: acquire(timeout>0) with no event loop degrades to timeout=0",
          "[async_semaphore]") {
    // No trantor loop attached to this thread -> can't schedule a timer.
    // Documented degradation: behave as if timeout were 0.
    astraea::AsyncSemaphore sem(1);
    std::optional<astraea::AsyncSemaphore::Permit> first;
    bool d_first = false;
    acquire_into(sem, first, d_first);
    REQUIRE(d_first);

    std::optional<std::optional<astraea::AsyncSemaphore::Permit>> p;
    bool d = false;
    try_acquire_timed(sem, std::chrono::milliseconds(100), p, d);
    REQUIRE(d);
    REQUIRE(p.has_value());
    REQUIRE_FALSE(p->has_value());
    REQUIRE(sem.waiters() == 0);
}
