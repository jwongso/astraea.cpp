#pragma once
//
// Coroutine concurrency utilities for Drogon/trantor:
//   when_all_pair  - concurrent two-task fanout
//   pin_to_loop    - re-pin a coroutine to a specific trantor event loop
//
// Drogon 1.9.7 has no built-in when_all/gather combinator. This header
// provides a minimal typed two-task variant covering the one use case in
// assemble_request (pipeline.retrieve + retrieve_anchor).
//
// Race safety: same mutex-guarded waiter pattern as AsyncSemaphore and
// AcquireTimedAwaiter - notify_one() checks remaining under the lock, so
// a completion arriving between await_ready() and await_suspend() cannot
// silently drop the resume.
//
// Lifetime: state is shared_ptr co-owned by both launched coroutines and
// the awaiter, so the coroutine frame outliving the awaiter (or vice versa)
// is safe.
//
#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <trantor/net/EventLoop.h>
#include <drogon/utils/coroutine.h>

namespace astraea::detail {

// ---------------------------------------------------------------------------
// pin_to_loop: re-pin the current coroutine to `loop`.
//
// await_ready() returns true immediately (no suspension) if the coroutine
// is already executing on `loop` - zero overhead on the common path.
// Otherwise it queues a resume via loop->queueInLoop so the continuation
// runs on `loop`.
//
// Typical use: after a when_all_pair or any co_await that may shift the
// coroutine to a different event-loop thread, call
//   co_await astraea::detail::pin_to_loop(client_loop);
// before calling ResponseStream::send(). This serialises all writes on the
// same thread as the client TCP connection's teardown, eliminating the
// trantor Channel::remove() assertion (events_ != kNoneEvent) race that
// fires when send() crosses event-loop thread boundaries.
// ---------------------------------------------------------------------------

inline auto pin_to_loop(trantor::EventLoop* loop) {
    struct PinAwaiter {
        trantor::EventLoop* loop;
        bool await_ready() const noexcept {
            return trantor::EventLoop::getEventLoopOfCurrentThread() == loop;
        }
        void await_suspend(std::coroutine_handle<> h) const {
            loop->queueInLoop([h]() mutable { h.resume(); });
        }
        void await_resume() const noexcept {}
    };
    return PinAwaiter{loop};
}

template<typename TA, typename TB>
struct WhenAllPairState {
    std::optional<TA>        a;
    std::optional<TB>        b;
    std::exception_ptr       err_a;
    std::exception_ptr       err_b;
    std::atomic<int>         remaining{2};
    std::mutex               mu;
    std::coroutine_handle<>  waiter;

    void notify_one() {
        std::coroutine_handle<> h;
        if (--remaining == 0) {
            std::lock_guard<std::mutex> lk(mu);
            h = waiter;
        }
        if (h) h.resume();
    }
};

template<typename TA, typename TB>
struct WhenAllPairAwaiter {
    std::shared_ptr<WhenAllPairState<TA, TB>> st;

    bool await_ready() noexcept { return st->remaining == 0; }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->remaining == 0) return false;
        st->waiter = h;
        return true;
    }
    std::pair<TA, TB> await_resume() {
        if (st->err_a) std::rethrow_exception(st->err_a);
        if (st->err_b) std::rethrow_exception(st->err_b);
        return {std::move(*st->a), std::move(*st->b)};
    }
};

// Run two independent Drogon Tasks concurrently. Returns when both complete.
// Exceptions from either task are rethrown in the awaiting coroutine (task A
// takes priority if both throw).
template<typename TA, typename TB>
drogon::Task<std::pair<TA, TB>> when_all_pair(
    drogon::Task<TA> ta, drogon::Task<TB> tb)
{
    auto state = std::make_shared<WhenAllPairState<TA, TB>>();

    drogon::async_run(
        [state, ta = std::move(ta)]() mutable -> drogon::Task<> {
            try { state->a.emplace(co_await std::move(ta)); }
            catch (...) { state->err_a = std::current_exception(); }
            state->notify_one();
        });
    drogon::async_run(
        [state, tb = std::move(tb)]() mutable -> drogon::Task<> {
            try { state->b.emplace(co_await std::move(tb)); }
            catch (...) { state->err_b = std::current_exception(); }
            state->notify_one();
        });

    co_return co_await WhenAllPairAwaiter<TA, TB>{state};
}

} // namespace astraea::detail
