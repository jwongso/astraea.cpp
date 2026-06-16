#pragma once
//
// Coroutine-aware counting semaphore for Drogon-based handlers.
//
// std::counting_semaphore::acquire() blocks the calling thread, which is fatal
// inside a coroutine running on a trantor I/O loop - it freezes every other
// connection scheduled on the same loop. AsyncSemaphore instead suspends the
// coroutine and resumes it on its original event loop when a permit becomes
// available, matching the same loop-affinity discipline that PR #11 enforces
// for SSE streams.
//
// Port target: serialize Generator::generate / generate_stream calls when
// LLM_GLOBAL_CONCURRENCY > 0 (Python core/api.py:553 global_llm_acquire).
// In-process only - cross-process coordination via Redis is a follow-up.
//
// Usage (untimed - blocks indefinitely):
//   auto permit = co_await sem.acquire();
//   co_await generator.generate(msgs);     // serialized by `sem`
//   // permit released when it goes out of scope (RAII)
//
// Usage (timed - returns nullopt if no permit within `timeout`):
//   auto maybe = co_await sem.acquire(std::chrono::seconds(90));
//   if (!maybe) co_return /* 503 backpressure */;
//   auto permit = std::move(*maybe);
//
// The timed overload matches Python core/api.py's global_llm_acquire(timeout=90.0)
// pattern: when the LLM is saturated, the request returns 503 instead of piling
// up indefinitely. Without a cap, a queue-buildup death spiral is one slow LLM
// call away.
//
// Thread-safety: all public methods are safe to call from any thread. Permit
// move / destruction may happen on a different thread than acquire(); release
// hops back to the original loop only when resuming a waiter (the captured
// loop is the waiter's, not the releaser's).
//
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace trantor { class EventLoop; }

namespace astraea {

/// @brief Coroutine-aware counting semaphore for Drogon-based handlers.
///
/// std::counting_semaphore::acquire() blocks the calling thread, which is fatal
/// inside a coroutine running on a trantor I/O loop. AsyncSemaphore instead
/// suspends the coroutine and resumes it on its original event loop when a
/// permit becomes available, preserving the loop-affinity required for safe
/// SSE streaming. Python parity: core/api.py global_llm_acquire().
///
/// Thread-safe: all public methods may be called from any thread. Permit
/// destruction may happen on a different thread than acquire(); the resume
/// hops back to the original waiter's event loop.
class AsyncSemaphore {
public:
    explicit AsyncSemaphore(int permits) noexcept;
    AsyncSemaphore(const AsyncSemaphore&)            = delete;
    AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;
    AsyncSemaphore(AsyncSemaphore&&)                 = delete;
    AsyncSemaphore& operator=(AsyncSemaphore&&)      = delete;

    /// @brief RAII permit returned by acquire(); releases the semaphore slot on destruction.
    ///
    /// Move-only. Default-constructed Permits are empty (held() == false) and
    /// release nothing; safe to use as guard variables that may or may not own
    /// a permit.
    class Permit {
    public:
        Permit() noexcept = default;
        explicit Permit(AsyncSemaphore* s) noexcept : _s(s) {}
        Permit(const Permit&)            = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& o) noexcept : _s(o._s) { o._s = nullptr; }
        Permit& operator=(Permit&& o) noexcept {
            if (this != &o) { reset(); _s = o._s; o._s = nullptr; }
            return *this;
        }
        ~Permit() { reset(); }
        bool held() const noexcept { return _s != nullptr; }
        void reset() noexcept;
    private:
        AsyncSemaphore* _s = nullptr;
    };

    /// @brief Untimed acquire awaiter; suspends the coroutine until a slot is available.
    struct AcquireAwaiter {
        AsyncSemaphore* sem;
        bool await_ready() noexcept;
        // noexcept: enqueueing a waiter can throw std::bad_alloc on OOM
        // (std::deque::push_back). The noexcept here forces std::terminate
        // on OOM rather than propagating - acceptable because there is no
        // recovery path inside an awaiter, and waiter structs are tiny.
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        Permit await_resume() noexcept { return Permit{sem}; }
    };
    AcquireAwaiter acquire() noexcept { return AcquireAwaiter{this}; }

    /// @brief Timed acquire; returns nullopt if no permit is available within `timeout`.
    ///
    /// timeout == 0 means try-once: return nullopt immediately on contention
    /// without scheduling a timer. A non-zero timeout schedules a resume via
    /// the trantor event loop so the coroutine does not block its I/O thread.
    /// On threads without a trantor loop (e.g. unit tests) behaves like timeout==0.

    // Implementation detail - declaration here so AcquireTimedAwaiter can
    // refer to it; full definition appears below the AsyncSemaphore class.
    // Tracked under shared_ptr so both the semaphore queue, the timer
    // callback, and the awaiter can hold non-owning-by-default references
    // without lifetime concerns. State mutation under AsyncSemaphore::_mu only.
    class WaiterState;

    /// @brief Timed acquire awaiter; suspends or returns nullopt on timeout.
    struct AcquireTimedAwaiter {
        AsyncSemaphore*           sem;
        std::chrono::milliseconds timeout;
        // Populated in await_ready/await_suspend - tells await_resume which
        // path got us here so it knows whether to return a Permit or nullopt.
        bool                      _fast_path = false;
        // Strong ref to the Waiter so destruction of the awaiter (after
        // co_return) doesn't pull the Waiter out from under a still-scheduled
        // timer. Timer callback also holds a shared_ptr; whoever is last out
        // destroys it.
        std::shared_ptr<WaiterState> _waiter;

        bool await_ready() noexcept;
        bool await_suspend(std::coroutine_handle<> h) noexcept;
        std::optional<Permit> await_resume() noexcept;
    };
    AcquireTimedAwaiter acquire(std::chrono::milliseconds timeout) noexcept {
        // Explicit zero-initialise the defaulted trailing fields: Clang's
        // -Wmissing-field-initializers (under -Werror in dev preset) fires
        // on aggregate init that supplies fewer args than the field count,
        // even when default member initializers are present.
        return AcquireTimedAwaiter{this, timeout, /*_fast_path=*/false, /*_waiter=*/{}};
    }

    int available() const noexcept; ///< Current available permits; for /healthz and unit tests.
    int waiters() const noexcept; ///< Current coroutines waiting for a permit; for /healthz and unit tests.

private:
    friend class Permit;
    friend struct AcquireAwaiter;
    friend struct AcquireTimedAwaiter;

    bool try_acquire() noexcept;
    void release() noexcept;

    mutable std::mutex _mu;
    int                _avail;
    std::deque<std::shared_ptr<WaiterState>> _waiters;
};

/// @brief Shared mutable state for a single timed acquire waiter.
///
/// Owned by both the semaphore's waiter queue and the timer callback via
/// shared_ptr so neither can outlive the other. State is mutated only under
/// AsyncSemaphore::_mu.
class AsyncSemaphore::WaiterState {
public:
    std::coroutine_handle<>   h; ///< Coroutine to resume when a permit is granted or the timer fires.
    trantor::EventLoop*       loop = nullptr; ///< Event loop to resume on; matches the acquiring coroutine's loop.
    std::uint64_t             timer_id = 0; ///< Trantor timer ID; 0 means no timer (untimed acquire).
    bool                      acquired  = false; ///< Set to true when a permit was granted before the timer fired.
    bool                      timed_out = false; ///< Set to true when the timer fired before a permit was available.
};

} // namespace astraea
