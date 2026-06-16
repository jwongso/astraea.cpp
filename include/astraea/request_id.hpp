#pragma once
// Request-ID helpers: accept-or-generate for X-Request-Id correlation.
#include <string>

namespace astraea {

/// @brief Return true when `id` is a safe, well-formed X-Request-Id value.
///
/// Accepts 1-64 chars, each alphanumeric, '-', or '_'. Rejects whitespace,
/// control chars, slashes, and empty strings so request IDs cannot inject
/// unexpected text into log files.
bool valid_request_id(const std::string& id) noexcept;

/// @brief Accept `inbound` if valid, or generate a fresh UUID4 as a fallback.
///
/// Returns `inbound` when it passes valid_request_id(); otherwise returns a
/// freshly generated lowercase hyphenated UUID4 via drogon::utils::getUuid.
/// Always returns a non-empty safe string.
std::string resolve_request_id(const std::string& inbound);

} // namespace astraea
