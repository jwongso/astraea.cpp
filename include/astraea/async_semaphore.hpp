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
// Usage:
//   auto permit = co_await sem.acquire();
//   co_await generator.generate(msgs);     // serialized by `sem`
//   // permit released when it goes out of scope (RAII)
//
// Thread-safety: all public methods are safe to call from any thread. Permit
// move / destruction may happen on a different thread than acquire(); release
// hops back to the original loop only when resuming a waiter (the captured
// loop is the waiter's, not the releaser's).
//
#include <coroutine>
#include <deque>
#include <mutex>

namespace trantor { class EventLoop; }

namespace astraea {

class AsyncSemaphore {
public:
    explicit AsyncSemaphore(int permits) noexcept;
    AsyncSemaphore(const AsyncSemaphore&)            = delete;
    AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;
    AsyncSemaphore(AsyncSemaphore&&)                 = delete;
    AsyncSemaphore& operator=(AsyncSemaphore&&)      = delete;

    // RAII permit. Releases on destruction; moveable but not copyable.
    // Default-constructed Permits are empty (held() == false) and release
    // nothing on destruction - used as the result of guard variables that
    // may or may not own a permit.
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

    // Test / introspection.
    int available() const noexcept;
    int waiters() const noexcept;

private:
    friend class Permit;
    friend struct AcquireAwaiter;

    bool try_acquire() noexcept;
    void release() noexcept;

    struct Waiter {
        std::coroutine_handle<> h;
        trantor::EventLoop*     loop;
    };

    mutable std::mutex _mu;
    int                _avail;
    std::deque<Waiter> _waiters;
};

} // namespace astraea
