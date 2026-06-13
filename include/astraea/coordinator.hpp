#pragma once
//
// Abstract interface for distributed-permit coordination, so the application
// layer (handlers, RAGPipeline) doesn't bake in any one coordination backend.
//
// Why an interface (not just AsyncSemaphore directly):
//   - InProcessCoordinator: single-binary deployments. Wraps the in-process
//     AsyncSemaphore. Zero new dependencies. The default.
//   - RedisCoordinator (Phase 2): cross-process / multi-host. Sync hiredis +
//     thread offload + runAfter-driven coroutine polling. Adds libhiredis.
//   - Future: SidecarCoordinator (Envoy local-rate-limit), EtcdCoordinator,
//     in-memory mock for tests, etc. All conform to this one shape.
//
// The interface is deliberately small: untimed + timed acquire, plus a
// configured max_concurrency for /healthz introspection. No publish-subscribe,
// no rate-curve queries, no per-permit metadata. Those belong in higher-level
// observability, not in the synchronisation primitive.
//
// Permit ownership is RAII via std::unique_ptr<PermitImpl> - the destructor
// virtual-dispatches into the backend-specific release. Move-only; safe to
// hold across co_await suspensions.
//
#include <chrono>
#include <memory>
#include <optional>

#include <drogon/utils/coroutine.h>

namespace astraea {

class CoordinatorClient {
public:
    virtual ~CoordinatorClient() = default;

    // Backend-specific permit state. Destruction releases the permit. Concrete
    // backends derive (AsyncSemaphore::Permit for in-process, hiredis Lua-
    // release for Redis, etc.). Users never touch PermitImpl directly - they
    // only see the type-erased Permit wrapper below.
    class PermitImpl {
    public:
        virtual ~PermitImpl() = default;
    };

    // RAII permit. Move-only. Releases the underlying coordination state on
    // destruction or reset(). Empty-default state (no held resources) is OK
    // for guard variables.
    class Permit {
    public:
        Permit() noexcept = default;
        explicit Permit(std::unique_ptr<PermitImpl> impl) noexcept
            : _impl(std::move(impl)) {}
        Permit(const Permit&)            = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&&) noexcept            = default;
        Permit& operator=(Permit&&) noexcept = default;
        ~Permit() = default;            // _impl's virtual dtor does the release

        bool held() const noexcept { return _impl != nullptr; }
        void reset() noexcept       { _impl.reset(); }
    private:
        std::unique_ptr<PermitImpl> _impl;
    };

    // Untimed acquire - blocks until a permit is available. Use sparingly:
    // for a network-backed coordinator this can pin a coroutine indefinitely
    // if the backend is unreachable. Prefer the timed overload in production.
    virtual drogon::Task<Permit> acquire() = 0;

    // Timed acquire. timeout == 0 = try once and return nullopt on contention.
    // Returns std::nullopt if no permit became available within `timeout`.
    virtual drogon::Task<std::optional<Permit>> acquire(
        std::chrono::milliseconds timeout) = 0;

    // Configured permit count. Useful for /healthz and for sanity-checking
    // against LLM server parallelism on startup.
    virtual int max_concurrency() const noexcept = 0;

    // Identifier for logging / metrics dimensions. e.g. "in_process", "redis".
    virtual const char* backend_name() const noexcept = 0;
};

} // namespace astraea
