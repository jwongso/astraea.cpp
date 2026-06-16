#pragma once
//
// CoordinatorClient backed by the in-process AsyncSemaphore.
//
// This is the default backend - zero network deps, zero new dependencies,
// matches the pre-coordinator semantics exactly. Use for single-binary
// deployments where LLM_GLOBAL_CONCURRENCY is the only cap that matters.
//
// For multi-host / multi-process deployments where several `nz_tenancy`
// binaries share an LLM server, use RedisCoordinator (Phase 2) instead.
//
#include "astraea/async_semaphore.hpp"
#include "astraea/coordinator.hpp"

#include <memory>
#include <utility>

namespace astraea {

/// @brief CoordinatorClient backed by the in-process AsyncSemaphore.
///
/// Default backend for single-binary deployments. Zero network dependencies;
/// matches the pre-coordinator semantics exactly. For multi-host or multi-
/// process deployments where several app binaries share one LLM server, use
/// RedisCoordinator instead so the global concurrency cap is cluster-wide.
class InProcessCoordinator final : public CoordinatorClient {
public:
    explicit InProcessCoordinator(int permits)
        : _sem(permits), _max(permits) {}

    drogon::Task<Permit> acquire() override {
        auto inner = co_await _sem.acquire();
        co_return Permit{std::make_unique<InProcessPermit>(std::move(inner))};
    }

    drogon::Task<std::optional<Permit>> acquire(
        std::chrono::milliseconds timeout) override
    {
        auto maybe = co_await _sem.acquire(timeout);
        if (!maybe) co_return std::nullopt;
        co_return Permit{std::make_unique<InProcessPermit>(std::move(*maybe))};
    }

    int max_concurrency() const noexcept override { return _max; }
    const char* backend_name() const noexcept override { return "in_process"; }

    int available() const noexcept { return _sem.available(); } ///< Current available permit count; for /healthz and unit tests.
    int waiters()   const noexcept { return _sem.waiters(); } ///< Current coroutines waiting for a permit; for /healthz and unit tests.

private:
    /// @brief Concrete PermitImpl that holds the AsyncSemaphore::Permit by value.
    ///
    /// The virtual destructor drives the semaphore release when the outer
    /// CoordinatorClient::Permit is destroyed or reset().
    struct InProcessPermit final : PermitImpl {
        AsyncSemaphore::Permit inner;
        explicit InProcessPermit(AsyncSemaphore::Permit p) noexcept
            : inner(std::move(p)) {}
        // ~InProcessPermit defaulted -> inner.~Permit() releases the semaphore slot
    };

    AsyncSemaphore _sem;
    int            _max;
};

} // namespace astraea
