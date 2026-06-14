#pragma once
// Request-ID helpers: accept-or-generate for X-Request-Id correlation.
#include <string>

namespace astraea {

// Returns true if `id` is a syntactically valid request ID: 1-64 chars,
// each alphanumeric or '-' or '_'. Rejects anything else (including
// whitespace, control chars, slashes - clients should not inject random
// text into our log files).
bool valid_request_id(const std::string& id) noexcept;

// Returns `inbound` if it passes valid_request_id, else a freshly
// generated UUID4 (lowercase, hyphenated) via drogon::utils::getUuid.
std::string resolve_request_id(const std::string& inbound);

} // namespace astraea
