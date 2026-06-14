#include "astraea/request_id.hpp"

#include <drogon/utils/Utilities.h>

#include <cctype>

namespace astraea {

bool valid_request_id(const std::string& id) noexcept {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const auto u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string resolve_request_id(const std::string& inbound) {
    if (valid_request_id(inbound)) return inbound;
    return drogon::utils::getUuid();
}

} // namespace astraea
