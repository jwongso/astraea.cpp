#pragma once
#include <stdexcept>
#include <string>

namespace astraea {

/// @brief Exception thrown by sanitize_question() on any input violation.
///
/// Carries the HTTP status code the handler should return (always 400 in
/// practice, but extensible if future checks warrant a 413 for length).
struct SanitizeError : std::runtime_error {
    int http_status; ///< HTTP status code the handler should forward to the client.
    explicit SanitizeError(const std::string& msg, int status = 400)
        : std::runtime_error(msg), http_status(status) {}
};

/// @brief Strip control characters, enforce length, and detect prompt injection.
///
/// Throws SanitizeError(400) on any violation. Direct port of Python
/// core/api.py:sanitize_question(). The default max_chars matches the
/// JurisdictionBase::max_question_chars() default and Config::max_body_bytes
/// is set to allow it comfortably.
std::string sanitize_question(std::string_view text, int max_chars = 1200);

} // namespace astraea
