#pragma once
//
// Idle trantor::TcpClient pool for the LLM streaming path, shared across
// Generator instances (held by shared_ptr; multiple Generators can pass
// the same pool pointer to reuse connections across them - e.g. the main
// generator and the rewrite generator could share a pool against the
// same llama-server endpoint). Lets keep-alive on /chat/completions
// actually save TCP setup + DNS resolution + (for remote endpoints) TLS
// handshake by reusing connections across requests instead of opening a
// fresh socket each time.
//
// Keyed by (event loop, "host:port"). trantor::TcpClient is loop-bound at
// construction, so pooled entries never cross loops; per-loop sub-maps are
// guarded by a single mutex on the pool. The pool is intentionally small
// and dumb - no idle TTL, no eviction policy. release() only accepts
// clients with a still-connected TcpConnection; try_acquire() drops dead
// entries it finds. That's enough to let happy-path traffic recycle a
// handful of long-lived connections without growing unbounded.
//
// Concurrency / safety model:
//   - Multiple LlmStreamSessions on different loops may acquire / release
//     concurrently. The mutex serialises map access only; held very briefly.
//   - try_acquire() and release() are loop-agnostic: callers pass the
//     loop they're running on; we never touch trantor data structures
//     under the lock.
//   - Sessions on the SAME loop are inherently serial (trantor loops are
//     single-threaded). So at most one session per (loop, endpoint) is
//     ever in flight simultaneously - the per-loop vector exists for
//     forward-compatibility with future LLM_GLOBAL_CONCURRENCY > N_loops
//     deployments and as a defensive buffer for the rare overlap window.
//
// Lifecycle invariants:
//   - Callers MUST clear all callbacks on a returned client BEFORE calling
//     release() - otherwise the next acquirer's session would receive
//     stale callbacks bound to a finished session.
//   - Callers MUST release ONLY when the HTTP response body is fully
//     consumed (typically HttpStreamParser::State::Done). Releasing while
//     bytes are still in flight would cause the next request to see
//     leftover bytes prepended to its response - parse error guaranteed.
//
// What the pool does NOT do:
//   - It does not DNS-resolve or connect on behalf of callers. TcpClient
//     construction + connect() stays at the caller; the pool only stores
//     already-connected idle clients.
//   - It does not detect server-side close while idle. try_acquire()'s
//     `connection().connected()` check is best-effort - the next send
//     after acquire might still fail if the server closed milliseconds
//     before. Callers must handle send failure on a pooled connection
//     by falling back to a fresh one.
//

#include <trantor/net/EventLoop.h>
#include <trantor/net/TcpClient.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace astraea::detail {

class LlmTcpPool {
public:
    LlmTcpPool() = default;

    LlmTcpPool(const LlmTcpPool&)            = delete;
    LlmTcpPool& operator=(const LlmTcpPool&) = delete;

    // Acquire an idle client for (loop, endpoint_key). Returns nullptr if
    // the pool has no entries or all candidates are disconnected. Callers
    // MUST rebind their own setMessageCallback / setConnectionCallback /
    // setConnectionErrorCallback before sending a request on the returned
    // client - the cleared no-op callbacks installed by release() will
    // otherwise silently swallow all incoming bytes.
    std::shared_ptr<trantor::TcpClient> try_acquire(
        trantor::EventLoop* loop, const std::string& endpoint_key);

    // Return a client to the pool. Caller has cleared all callbacks and the
    // HTTP response body for the last request is fully drained. Disconnected
    // clients are silently dropped (not returned) so try_acquire() never
    // hands out a known-dead entry. Null clients are silently ignored.
    void release(trantor::EventLoop* loop, const std::string& endpoint_key,
                 std::shared_ptr<trantor::TcpClient> client);

    // Approximate count of pooled clients (for /healthz and tests). Cheap
    // O(pool size) walk; not in the hot path.
    std::size_t size() const;

private:
    mutable std::mutex _mu;
    // Outer key = trantor event loop (each loop = one I/O thread).
    // Inner key = "host:port" endpoint string.
    // Value = idle clients available for reuse on that (loop, endpoint).
    std::unordered_map<trantor::EventLoop*,
        std::unordered_map<std::string,
            std::vector<std::shared_ptr<trantor::TcpClient>>>> _idle;
};

} // namespace astraea::detail
