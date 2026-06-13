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

    // Test / introspection - exposed only on the concrete type so the
    // interface stays minimal. Used by /healthz and unit tests.
    int available() const noexcept { return _sem.available(); }
    int waiters()   const noexcept { return _sem.waiters(); }

private:
    // Concrete permit shim: holds the AsyncSemaphore::Permit by value so the
    // virtual destructor here drives the release. Move-only via the inner
    // AsyncSemaphore::Permit.
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
