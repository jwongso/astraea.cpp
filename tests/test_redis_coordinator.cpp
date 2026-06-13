#include "astraea/redis_coordinator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <trantor/net/EventLoop.h>
#include <trantor/net/EventLoopThread.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <optional>
#include <string>

namespace {

// Skip RedisCoordinator tests when REDIS_HOST is unset. Local dev without a
// Redis instance still gets the rest of the test suite green; CI sets the
// env var via the redis service container.
std::optional<std::string> redis_url_from_env() {
    const char* host = std::getenv("REDIS_HOST");
    if (!host || !*host) return std::nullopt;
    const char* port = std::getenv("REDIS_PORT");
    std::string url = "redis://";
    url += host;
    url += ":";
    url += (port && *port) ? port : "6379";
    url += "/0";
    return url;
}

// Drive a Drogon coroutine to completion via a trantor loop running in its
// own thread. Returns the result via a std::promise so the test thread can
// block on it without spinning a Drogon app.
template<typename T>
T run_coro_blocking(trantor::EventLoop* loop, drogon::Task<T> task) {
    std::promise<T> p;
    auto fut = p.get_future();
    loop->runInLoop([&p, t = std::move(task)]() mutable {
        drogon::async_run([&p, t = std::move(t)]() mutable -> drogon::Task<> {
            try {
                if constexpr (std::is_void_v<T>) {
                    co_await std::move(t);
                    p.set_value();
                } else {
                    auto v = co_await std::move(t);
                    p.set_value(std::move(v));
                }
            } catch (...) {
                p.set_exception(std::current_exception());
            }
        });
    });
    return fut.get();
}

} // namespace

TEST_CASE("RedisCoordinator: acquire-release single permit roundtrip",
          "[redis_coordinator][integration]")
{
    auto url = redis_url_from_env();
    if (!url) {
        SKIP("REDIS_HOST not set (no Redis service available)");
    }

    trantor::EventLoopThread loop_thread;
    loop_thread.run();

    astraea::RedisCoordinator coord(*url, /*max_concurrency=*/1,
                                     std::chrono::milliseconds(50),
                                     std::chrono::seconds(30));
    REQUIRE(std::string(coord.backend_name()) == "redis");
    REQUIRE(coord.max_concurrency() == 1);

    // Acquire a permit; it should arrive quickly (no contention against a
    // fresh state on the CI redis service).
    auto maybe = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::seconds(2)));

    REQUIRE(maybe.has_value());
    REQUIRE(maybe->held());

    // Release happens when the Permit goes out of scope; verify the next
    // acquire still succeeds (i.e. DECR cleaned up the counter).
    maybe.reset();

    auto again = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::seconds(2)));

    REQUIRE(again.has_value());
    REQUIRE(again->held());
}

TEST_CASE("RedisCoordinator: second acquire times out when at capacity",
          "[redis_coordinator][integration]")
{
    auto url = redis_url_from_env();
    if (!url) SKIP("REDIS_HOST not set");

    trantor::EventLoopThread loop_thread;
    loop_thread.run();

    astraea::RedisCoordinator coord(*url, /*max_concurrency=*/1,
                                     std::chrono::milliseconds(50),
                                     std::chrono::seconds(30));

    auto first = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::seconds(2)));
    REQUIRE(first.has_value());

    // Second acquire with a short timeout while the first still holds the
    // permit should return nullopt after the deadline.
    const auto t0 = std::chrono::steady_clock::now();
    auto second = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::milliseconds(300)));
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    REQUIRE_FALSE(second.has_value());
    // We polled for ~300 ms. Allow generous slack for thread scheduling jitter
    // in CI (broadly bounded by 1 s so a real bug doesn't pass silently).
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= 250);
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 2000);

    // Cleanup: release the first permit so we don't pollute subsequent tests.
    first.reset();
}

TEST_CASE("RedisCoordinator: release allows a waiting acquire to succeed",
          "[redis_coordinator][integration]")
{
    auto url = redis_url_from_env();
    if (!url) SKIP("REDIS_HOST not set");

    trantor::EventLoopThread loop_thread;
    loop_thread.run();

    astraea::RedisCoordinator coord(*url, /*max_concurrency=*/1,
                                     std::chrono::milliseconds(50),
                                     std::chrono::seconds(30));

    auto first = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::seconds(2)));
    REQUIRE(first.has_value());

    // Release in 200 ms; the second acquire (with a 2 s deadline) should
    // pick up the slot within ~250 ms (one poll interval after release).
    std::atomic<bool> released = false;
    std::thread releaser([&first, &released]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        first.reset();
        released = true;
    });

    auto second = run_coro_blocking<std::optional<astraea::CoordinatorClient::Permit>>(
        loop_thread.getLoop(),
        coord.acquire(std::chrono::seconds(2)));
    releaser.join();

    REQUIRE(released);
    REQUIRE(second.has_value());
    REQUIRE(second->held());
}
