#include "astraea/sanitize.hpp"
#include <regex>

namespace astraea {

// Compiled once at startup. Mirrors Python regex patterns exactly.

static const std::regex INJECTION_RE{
    R"(ignore\s+(previous|all|prior|above)\s+(instructions?|rules?|prompts?))"
    R"(|forget\s+(previous|all|prior|above)\s+(instructions?|rules?|prompts?))"
    R"(|you\s+are\s+now\s+(a\s+|an\s+)?)"
    R"(|act\s+as\s+(if\s+)?(you\s+are\s+)?)"
    R"(|pretend\s+(you|to\s+be))"
    R"(|system\s*prompt\s*:)"
    R"(|<\s*system\s*>)",
    std::regex::icase
};

// Street types for address-only detection.
static const std::string ST_PAT =
    R"(st(?:reet)?|rd|road|ave(?:nue)?|dr(?:ive)?)"
    R"(|lane|pl(?:ace)?|cres(?:cent)?|tce|terrace)"
    R"(|way|cl(?:ose)?|ct|court)";

static const std::regex ADDRESS_ONLY_RE{
    R"(^(\d+\s+)?(?:[\w'\-]{1,25}\s+){1,4}(?:)" + ST_PAT + R"()\b[\s,]*(?:[\w][\w\s,]{0,60})?$)",
    std::regex::icase
};

static const std::regex LEGAL_TERMS_RE{
    R"(\b(?:landlord|tenant|bond|rent(?:al)?|notice|lease|damage|repair|)"
    R"(evict|rights?|tribunal|rta|section|s\d+|claim|dispute|agreement|)"
    R"(inspection|compensation|termination|fixed.?term|periodic|flat|)"
    R"(property|house|home|breach|arrear|week|month|pay|owe|liable|)"
    R"(habitable|healthy.?homes?|deposit|contract)\b)",
    std::regex::icase
};

// ---------------------------------------------------------------------------
// Unicode category helpers - mirrors Python's unicodedata.category Cc/Cf filter.
//
// Single-byte (ASCII):
//   Cc C0: 0x00-0x1F (except \t 0x09, \n 0x0A), DEL 0x7F
//
// Multi-byte strips (manually mapped from Unicode database):
//   C1 controls  U+0080-U+009F  Cc  C2 80..C2 9F
//   Soft hyphen  U+00AD         Cf  C2 AD
//   Arabic LM    U+061C         Cf  D8 9C
//   Zero-width   U+200B-U+200F  Cf  E2 80 8B..8F
//   Bidi ovrd    U+202A-U+202E  Cf  E2 80 AA..AE  (prompt-injection vector)
//   Word joiner  U+2060-U+2064  Cf  E2 81 A0..A4
//   Bidi isolate U+2066-U+2069  Cf  E2 81 A6..A9  (prompt-injection vector)
//   BOM          U+FEFF         Cf  EF BB BF
// ---------------------------------------------------------------------------

static bool is_strip_char(unsigned char c) {
    if (c <= 0x1F && c != 0x09 && c != 0x0A) return true;
    if (c == 0x7F) return true;
    return false;
}

std::string sanitize_question(std::string_view text, int max_chars) {
    // Strip Cc/Cf characters
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ) {
        const auto b = static_cast<unsigned char>(text[i]);

        if (b < 0x80) {
            if (!is_strip_char(b)) out += static_cast<char>(b);
            ++i;
            continue;
        }

        // C1 controls U+0080-U+009F (Cc): C2 80..C2 9F
        // Soft hyphen U+00AD (Cf):         C2 AD
        if (b == 0xC2 && i + 1 < text.size()) {
            const auto b1 = static_cast<unsigned char>(text[i + 1]);
            if ((b1 >= 0x80 && b1 <= 0x9F) || b1 == 0xAD) { i += 2; continue; }
        }

        // Arabic letter mark U+061C (Cf): D8 9C
        if (b == 0xD8 && i + 1 < text.size()
                && static_cast<unsigned char>(text[i + 1]) == 0x9C) {
            i += 2; continue;
        }

        // E2-prefix multi-byte ranges
        if (b == 0xE2 && i + 2 < text.size()) {
            const auto b1 = static_cast<unsigned char>(text[i + 1]);
            const auto b2 = static_cast<unsigned char>(text[i + 2]);
            if (b1 == 0x80) {
                if (b2 >= 0x8B && b2 <= 0x8F) { i += 3; continue; } // U+200B-200F
                if (b2 >= 0xAA && b2 <= 0xAE) { i += 3; continue; } // U+202A-202E bidi overrides
            }
            if (b1 == 0x81) {
                if (b2 >= 0xA0 && b2 <= 0xA4) { i += 3; continue; } // U+2060-2064 word joiners
                if (b2 >= 0xA6 && b2 <= 0xA9) { i += 3; continue; } // U+2066-2069 bidi isolates
            }
        }

        // BOM U+FEFF: EF BB BF
        if (b == 0xEF && i + 2 < text.size()
                && static_cast<unsigned char>(text[i + 1]) == 0xBB
                && static_cast<unsigned char>(text[i + 2]) == 0xBF) {
            i += 3; continue;
        }

        // All other multi-byte sequences pass through
        out += static_cast<char>(b);
        ++i;
    }

    // Trim (ASCII whitespace - same as Python .strip() for typical legal-question input)
    const auto first = out.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        throw SanitizeError("Question must not be empty.");
    const auto last = out.find_last_not_of(" \t\n\r");
    out.erase(last + 1);
    out.erase(0, first);

    if (out.empty())
        throw SanitizeError("Question must not be empty.");

    if (static_cast<int>(out.size()) > max_chars)
        throw SanitizeError("Question too long (max " + std::to_string(max_chars) + " characters).");

    if (std::regex_search(out, INJECTION_RE))
        throw SanitizeError("Question contains content that cannot be processed.");

    if (static_cast<int>(out.size()) <= 80
            && !std::regex_search(out, LEGAL_TERMS_RE)
            && std::regex_match(out, ADDRESS_ONLY_RE))
        throw SanitizeError(
            "This looks like a property address rather than a legal question. "
            "Try describing your situation instead - for example: "
            "'My landlord hasn't fixed the heating at my rental.'");

    return out;
}

} // namespace astraea
