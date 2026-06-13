#include "astraea/async_semaphore.hpp"

#include <trantor/net/EventLoop.h>

#include <algorithm>
#include <utility>

namespace astraea {

AsyncSemaphore::AsyncSemaphore(int permits) noexcept
    : _avail(permits) {}

bool AsyncSemaphore::try_acquire() noexcept {
    std::lock_guard<std::mutex> lock(_mu);
    if (_avail > 0) {
        --_avail;
        return true;
    }
    return false;
}

void AsyncSemaphore::release() noexcept {
    std::shared_ptr<AsyncSemaphore::WaiterState> w;
    {
        std::lock_guard<std::mutex> lock(_mu);
        // Skip any front entries already claimed by a racing timer (timed_out
        // waiters that haven't been removed yet) - shouldn't happen because
        // the timer removes itself from the queue under the lock, but defensive.
        while (!_waiters.empty()) {
            auto& front = _waiters.front();
            if (front->acquired || front->timed_out) {
                _waiters.pop_front();
                continue;
            }
            // Hand the permit directly without touching _avail - prevents a
            // fast try_acquire() from a third party from stealing the slot
            // between our increment and the waiter's resume.
            front->acquired = true;
            w = front;
            _waiters.pop_front();
            break;
        }
        if (!w) {
            ++_avail;
            return;
        }
    }

    // Cancel any pending timeout outside the lock - invalidateTimer takes its
    // own trantor lock and we don't want to nest them.
    if (w->timer_id && w->loop) {
        w->loop->invalidateTimer(static_cast<trantor::TimerId>(w->timer_id));
    }

    // Resume on the waiter's original loop so any subsequent socket I/O
    // (e.g. SSE stream->send()) fires from the thread that owns the
    // connection - same loop-affinity discipline as PR #11. If the waiter
    // never had a loop (unit test on a plain thread), resume directly.
    auto h = w->h;
    auto* here = trantor::EventLoop::getEventLoopOfCurrentThread();
    if (w->loop && w->loop != here) {
        w->loop->queueInLoop([h]() { h.resume(); });
    } else {
        h.resume();
    }
}

// ---------------------------------------------------------------------------
// AcquireAwaiter (untimed)
// ---------------------------------------------------------------------------

bool AsyncSemaphore::AcquireAwaiter::await_ready() noexcept {
    return sem->try_acquire();
}

bool AsyncSemaphore::AcquireAwaiter::await_suspend(
    std::coroutine_handle<> h) noexcept
{
    auto w = std::make_shared<AsyncSemaphore::WaiterState>();
    w->h    = h;
    w->loop = trantor::EventLoop::getEventLoopOfCurrentThread();

    std::lock_guard<std::mutex> lock(sem->_mu);
    // Re-check under the lock: a release() could have happened between
    // await_ready returning false and us taking the lock. Without this,
    // we would enqueue a waiter that never gets woken (no one will
    // release after us if _avail > 0 was true at that instant).
    if (sem->_avail > 0) {
        --sem->_avail;
        return false; // skip suspend, await_resume returns a held permit
    }
    sem->_waiters.push_back(std::move(w));
    // Footgun: the handle is NOT lifetime-tracked on this UNTIMED path. If the
    // owning coroutine is destroyed while suspended here (e.g. caller hand-rolls
    // a cancellation), a later h.resume() in release() is undefined behaviour.
    // The timed variant (AcquireTimedAwaiter) is cancellation-aware via the
    // timer-driven queue removal - prefer it if cancellation semantics matter.
    // Current handlers always hold their permit to completion so this is not
    // reachable today.
    return true;
}

// ---------------------------------------------------------------------------
// AcquireTimedAwaiter
// ---------------------------------------------------------------------------

bool AsyncSemaphore::AcquireTimedAwaiter::await_ready() noexcept {
    if (sem->try_acquire()) {
        _fast_path = true;
        return true;
    }
    // timeout == 0 -> "try once, don't enqueue, return nullopt on failure".
    // We synthesise this by reporting ready (skip suspend) with no permit,
    // so await_resume sees neither _fast_path nor a populated _waiter and
    // returns nullopt.
    if (timeout.count() <= 0) return true;
    return false;
}

bool AsyncSemaphore::AcquireTimedAwaiter::await_suspend(
    std::coroutine_handle<> h) noexcept
{
    auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
    auto w = std::make_shared<AsyncSemaphore::WaiterState>();
    w->h    = h;
    w->loop = loop;
    _waiter = w; // visible to await_resume

    {
        std::lock_guard<std::mutex> lock(sem->_mu);
        if (sem->_avail > 0) {
            --sem->_avail;
            w->acquired = true;
            return false; // skip suspend, await_resume returns Permit
        }
        if (!loop) {
            // No event loop -> can't schedule a timer, so we cannot bound
            // the wait. Degrade to timeout=0 semantics: do NOT enqueue,
            // skip suspend, let await_resume return nullopt (w->acquired
            // is still false). The alternative - enqueuing without a timer -
            // would suspend the coroutine forever.
            return false;
        }
        sem->_waiters.push_back(w);
    }

    // Schedule the timeout outside the semaphore lock so we don't nest
    // sem->_mu under trantor's internal timer-queue lock.
    auto* sem_ptr = sem;
    const double seconds = static_cast<double>(timeout.count()) / 1000.0;
    const auto timer_id = loop->runAfter(seconds, [sem_ptr, w]() {
        std::coroutine_handle<> to_resume;
        {
            std::lock_guard<std::mutex> lock(sem_ptr->_mu);
            // Race with release(): release() may have set acquired=true
            // between us being scheduled and the timer firing. In that case
            // the waiter has already been resumed - do nothing.
            if (w->acquired || w->timed_out) return;
            // Remove ourselves from the queue.
            auto& q = sem_ptr->_waiters;
            auto it = std::find(q.begin(), q.end(), w);
            if (it != q.end()) q.erase(it);
            w->timed_out = true;
            to_resume = w->h;
        }
        if (!to_resume) return;
        auto* here = trantor::EventLoop::getEventLoopOfCurrentThread();
        if (w->loop && w->loop != here) {
            w->loop->queueInLoop([to_resume]() { to_resume.resume(); });
        } else {
            to_resume.resume();
        }
    });

    // Race window: between releasing the lock above and getting the timer_id
    // back, a release() may have already resumed our waiter. If so, cancel
    // the now-pointless timer. Take the lock again briefly to commit timer_id
    // atomically with the acquired-check.
    std::lock_guard<std::mutex> lock(sem->_mu);
    if (w->acquired || w->timed_out) {
        loop->invalidateTimer(timer_id);
    } else {
        w->timer_id = static_cast<std::uint64_t>(timer_id);
    }
    return true;
}

std::optional<AsyncSemaphore::Permit>
AsyncSemaphore::AcquireTimedAwaiter::await_resume() noexcept {
    if (_fast_path) return Permit{sem};
    if (_waiter && _waiter->acquired) return Permit{sem};
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Permit / introspection
// ---------------------------------------------------------------------------

void AsyncSemaphore::Permit::reset() noexcept {
    if (_s) {
        _s->release();
        _s = nullptr;
    }
}

int AsyncSemaphore::available() const noexcept {
    std::lock_guard<std::mutex> lock(_mu);
    return _avail;
}

int AsyncSemaphore::waiters() const noexcept {
    std::lock_guard<std::mutex> lock(_mu);
    return static_cast<int>(_waiters.size());
}

} // namespace astraea
