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

/// @brief Abstract interface for LLM-permit coordination across request handlers.
///
/// Decouples the application layer from any specific backend. Two concrete
/// implementations exist: InProcessCoordinator (single-binary AsyncSemaphore,
/// default) and RedisCoordinator (cluster-wide Lua counter). A third backend
/// (e.g. Envoy sidecar, in-memory mock) can be added by deriving from this
/// class without touching handler code.
///
/// Permit ownership is RAII via Permit, which holds a unique_ptr<PermitImpl>
/// whose virtual destructor dispatches into the backend-specific release logic.
/// Permits are move-only and safe to hold across co_await suspensions.
class CoordinatorClient {
public:
    virtual ~CoordinatorClient() = default;

    /// @brief Backend-specific permit state whose destructor releases the coordination resource.
    ///
    /// Concrete backends derive from this (AsyncSemaphore::Permit for in-process,
    /// hiredis Lua-release for Redis). Users interact only via the type-erased
    /// Permit wrapper below and never access PermitImpl directly.
    class PermitImpl {
    public:
        virtual ~PermitImpl() = default;
    };

    /// @brief RAII permit handle returned by acquire().
    ///
    /// Move-only. Releases the underlying coordination resource on destruction
    /// or reset(). The default-constructed state holds no resources and is
    /// safe to use as a guard variable that may or may not own a permit.
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

        bool held() const noexcept { return _impl != nullptr; } ///< True when this Permit owns a resource.
        void reset() noexcept       { _impl.reset(); } ///< Release the resource early.
    private:
        std::unique_ptr<PermitImpl> _impl;
    };

    /// @brief Untimed acquire; suspends the coroutine until a permit becomes available.
    ///
    /// For network-backed coordinators this can pin a coroutine indefinitely if
    /// the backend is unreachable. Prefer the timed overload in production.
    virtual drogon::Task<Permit> acquire() = 0;

    /// @brief Timed acquire; returns nullopt if no permit is available within `timeout`.
    ///
    /// timeout == 0 means try-once: return nullopt immediately on contention
    /// without suspending. Non-zero values schedule a resume via the trantor
    /// event loop so the coroutine does not block its I/O thread.
    virtual drogon::Task<std::optional<Permit>> acquire(
        std::chrono::milliseconds timeout) = 0;

    /// @brief Configured maximum permit count for /healthz introspection.
    virtual int max_concurrency() const noexcept = 0;

    /// @brief Short string identifying the backend for logging, e.g. "in_process" or "redis".
    virtual const char* backend_name() const noexcept = 0;
};

} // namespace astraea
