#pragma once
#include <stdexcept>
#include <string>

namespace astraea {

struct SanitizeError : std::runtime_error {
    int http_status;
    explicit SanitizeError(const std::string& msg, int status = 400)
        : std::runtime_error(msg), http_status(status) {}
};

// Strip control characters, enforce length, detect prompt injection.
// Throws SanitizeError(400) on any violation.
// Direct port of Python sanitize_question().
std::string sanitize_question(std::string_view text, int max_chars = 1200);

} // namespace astraea
