#include "astraea/detail/llm_tcp_pool.hpp"

#include <utility>

namespace astraea::detail {

std::shared_ptr<trantor::TcpClient> LlmTcpPool::try_acquire(
    trantor::EventLoop* loop, const std::string& endpoint_key)
{
    std::lock_guard lk(_mu);
    auto loop_it = _idle.find(loop);
    if (loop_it == _idle.end()) return nullptr;
    auto ep_it = loop_it->second.find(endpoint_key);
    if (ep_it == loop_it->second.end()) return nullptr;
    auto& vec = ep_it->second;
    // Drop disconnected candidates from the back so the search is O(dead)
    // worst-case and amortised O(1) on the happy path. Server-side close
    // while idle (typical for idle-timeout servers) is invisible to us
    // until we ask the TcpClient if its connection is still up.
    while (!vec.empty()) {
        auto client = std::move(vec.back());
        vec.pop_back();
        auto conn = client ? client->connection() : nullptr;
        if (conn && conn->connected()) {
            return client;
        }
        // Dead. Loop and try the next candidate. The dtor of `client`
        // here (when we drop out of scope without returning it) cleans
        // up trantor's per-client resources.
    }
    return nullptr;
}

void LlmTcpPool::release(trantor::EventLoop* loop, const std::string& endpoint_key,
                         std::shared_ptr<trantor::TcpClient> client)
{
    if (!client) return;
    auto conn = client->connection();
    if (!conn || !conn->connected()) return;  // dead, drop silently
    std::lock_guard lk(_mu);
    _idle[loop][endpoint_key].push_back(std::move(client));
}

std::size_t LlmTcpPool::size() const {
    std::lock_guard lk(_mu);
    std::size_t total = 0;
    for (const auto& [loop, by_endpoint] : _idle) {
        for (const auto& [endpoint, vec] : by_endpoint) {
            total += vec.size();
        }
    }
    return total;
}

} // namespace astraea::detail
