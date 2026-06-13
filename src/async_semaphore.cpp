#include "astraea/async_semaphore.hpp"

#include <trantor/net/EventLoop.h>

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
    std::coroutine_handle<> to_resume{};
    trantor::EventLoop*     waiter_loop = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mu);
        if (!_waiters.empty()) {
            // Hand the permit directly to the next waiter without
            // touching _avail - prevents a thundering-herd race where
            // a third party could try_acquire() between our increment
            // and the waiter's resume.
            auto w = _waiters.front();
            _waiters.pop_front();
            to_resume   = w.h;
            waiter_loop = w.loop;
        } else {
            ++_avail;
        }
    }
    if (!to_resume) return;

    // Resume the waiter on its original event loop so any subsequent
    // socket I/O (e.g. SSE stream->send()) fires from the thread that
    // owns the connection - same loop-affinity discipline as PR #11.
    // If the waiter never had a loop (e.g. unit test on a plain thread),
    // resume directly on the current thread.
    auto* here = trantor::EventLoop::getEventLoopOfCurrentThread();
    if (waiter_loop && waiter_loop != here) {
        waiter_loop->queueInLoop([to_resume]() { to_resume.resume(); });
    } else {
        to_resume.resume();
    }
}

bool AsyncSemaphore::AcquireAwaiter::await_ready() noexcept {
    return sem->try_acquire();
}

bool AsyncSemaphore::AcquireAwaiter::await_suspend(
    std::coroutine_handle<> h) noexcept
{
    std::lock_guard<std::mutex> lock(sem->_mu);
    // Re-check under the lock: a release() could have happened between
    // await_ready returning false and us taking the lock. Without this,
    // we would enqueue a waiter that never gets woken (no one will
    // release after us if _avail > 0 was true at that instant).
    if (sem->_avail > 0) {
        --sem->_avail;
        return false; // skip suspend, await_resume returns a held permit
    }
    sem->_waiters.push_back({
        h,
        trantor::EventLoop::getEventLoopOfCurrentThread(),
    });
    return true;
}

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
