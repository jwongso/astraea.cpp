#pragma once
//
// Request-ID helpers for correlation across logs, response headers, and JSONL
// entries.
//
// Wire pattern: every request handler does at its top:
//
//   const std::string req_id = astraea::resolve_request_id(req->getHeader("x-request-id"));
//
// If the client provided a valid X-Request-Id header (1-64 chars, alnum +
// dash + underscore), use it verbatim. Otherwise generate a fresh UUID4 via
// drogon::utils::getUuid(). The same string flows into:
//
//   - The X-Request-Id response header (echoed so clients can correlate)
//   - Every JSONL log entry's `request_id` field (QuestionLog, RouteDebug,
//     Feedback, TimingEvent)
//   - SPDLOG_WARN/ERROR messages at the handler level so grep-by-id works
//     across spdlog and JSONL together
//
// Validation matches SessionStore::valid_session_id (same character class)
// so the two IDs are interchangeable for logging purposes.
//
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
