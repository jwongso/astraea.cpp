#include "wiki_store.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>

namespace astraea::wiki {

// Default schema embedded so the binary works without a pre-existing schema file.
static constexpr std::string_view DEFAULT_SCHEMA = R"(# LLM Wiki Schema

## Page Naming
- Lowercase kebab-case: `bond-lodgement`, `rental-increase`, `landlord-obligations`
- Legislation pages: `rta-section-18`, `rta-part-2`
- Tribunal decisions: `case-smith-v-jones-2024`
- Maximum 50 characters

## Required Page Structure
```
# [Page Title]

## Summary
One paragraph describing what this page covers.

## Key Provisions
Bullet list of the most important rules, obligations, or facts.

## Related Pages
- [[related-page]] - brief note on the relationship

## Sources
- Source Name: what was taken from this source
```

## Internal Links
- Use [[page-name]] for all internal references
- Link every concept you mention
- Create links even if the target page does not exist yet

## Update Policy
- Merge new information into existing sections rather than replacing
- Always expand Related Pages when connections are found
- Update Sources to list every contributing document
)";

WikiStore::WikiStore(std::filesystem::path data_dir)
    : _data_dir(std::move(data_dir))
    , _wiki_dir(_data_dir / "wiki")
    , _raw_dir(_data_dir / "raw")
    , _schema_path(_data_dir / "SCHEMA.md")
    , _index_path(_wiki_dir / "INDEX.md")
    , _changelog_path(_wiki_dir / "CHANGELOG.md")
{}

void WikiStore::init()
{
    std::filesystem::create_directories(_wiki_dir);
    std::filesystem::create_directories(_raw_dir);
    if (!std::filesystem::exists(_schema_path)) {
        std::ofstream f(_schema_path);
        f << DEFAULT_SCHEMA;
    }
}

std::filesystem::path WikiStore::page_path(std::string_view name) const
{
    // Sanitize: strip any path separators so callers can't escape the wiki dir.
    std::string safe(name);
    for (auto& c : safe)
        if (c == '/' || c == '\\') c = '-';
    if (!safe.ends_with(".md")) safe += ".md";
    return _wiki_dir / safe;
}

std::optional<std::string> WikiStore::read_page(std::string_view name) const
{
    std::lock_guard lk(_mu);
    auto p = page_path(name);
    if (!std::filesystem::exists(p)) return std::nullopt;
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

void WikiStore::write_page(std::string_view name, std::string_view content)
{
    std::lock_guard lk(_mu);
    auto p = page_path(name);
    // Atomic write: temp file + rename so a crash mid-write doesn't corrupt.
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp);
        f << content;
    }
    std::filesystem::rename(tmp, p);
}

void WikiStore::delete_page(std::string_view name)
{
    std::lock_guard lk(_mu);
    std::filesystem::remove(page_path(name));
}

std::vector<std::string> WikiStore::list_pages() const
{
    std::lock_guard lk(_mu);
    std::vector<std::string> out;
    if (!std::filesystem::exists(_wiki_dir)) return out;
    for (auto& e : std::filesystem::directory_iterator(_wiki_dir)) {
        if (!e.is_regular_file()) continue;
        auto fn = e.path().filename().string();
        // Skip internal files.
        if (fn == "INDEX.md" || fn == "CHANGELOG.md") continue;
        if (fn.ends_with(".md"))
            out.push_back(fn.substr(0, fn.size() - 3));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string WikiStore::all_pages_text(std::size_t max_chars) const
{
    std::lock_guard lk(_mu);
    std::string out;
    out.reserve(std::min(max_chars, std::size_t{65536}));
    for (auto& e : std::filesystem::directory_iterator(_wiki_dir)) {
        if (!e.is_regular_file()) continue;
        auto fn = e.path().filename().string();
        if (fn == "INDEX.md" || fn == "CHANGELOG.md" || !fn.ends_with(".md")) continue;
        std::string name = fn.substr(0, fn.size() - 3);
        std::ifstream f(e.path());
        // Braces avoid the most vexing parse on istreambuf_iterator<char>().
        std::string text{std::istreambuf_iterator<char>(f),
                         std::istreambuf_iterator<char>{}};
        out += "\n=== " + name + " ===\n";
        out += text;
        if (out.size() >= max_chars) {
            out.resize(max_chars);
            out += "\n[...truncated for context limit]\n";
            break;
        }
    }
    return out;
}

std::vector<std::filesystem::path> WikiStore::list_raw() const
{
    std::lock_guard lk(_mu);
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(_raw_dir)) return out;
    for (auto& e : std::filesystem::recursive_directory_iterator(_raw_dir))
        if (e.is_regular_file()) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<std::string> WikiStore::read_raw(const std::filesystem::path& p) const
{
    std::lock_guard lk(_mu);
    if (!std::filesystem::exists(p)) return std::nullopt;
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

std::filesystem::path WikiStore::save_raw(std::string_view filename,
                                          const char* data, std::size_t size)
{
    std::lock_guard lk(_mu);
    std::string safe(filename);
    for (auto& c : safe)
        if (c == '/' || c == '\\' || c == '\0') c = '_';
    auto p = _raw_dir / safe;
    std::ofstream f(p, std::ios::binary);
    f.write(data, static_cast<std::streamsize>(size));
    return p;
}

std::optional<std::string> WikiStore::read_raw_as_text(const std::filesystem::path& p) const
{
    if (!std::filesystem::exists(p)) return std::nullopt;

    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".pdf") {
        // Escape single quotes in path to prevent shell injection.
        std::string safe = p.string();
        std::string escaped;
        escaped.reserve(safe.size() + 10);
        for (char c : safe) {
            if (c == '\'') escaped += "'\\''";
            else           escaped += c;
        }
        const std::string cmd = std::format("pdftotext -layout '{}' -", escaped);
        spdlog::info("[wiki] pdftotext: starting on '{}'", p.string());
        const auto t0 = std::chrono::steady_clock::now();
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            spdlog::error("[wiki] pdftotext: popen failed for '{}'", p.string());
            return std::nullopt;
        }
        std::string result;
        std::array<char, 4096> buf{};
        while (fgets(buf.data(), buf.size(), pipe))
            result += buf.data();
        const int rc = pclose(pipe);
        // Strip control characters that break JSON serialization
        // (form feed \f, vertical tab \v, null \0, etc.). Keep \t \n \r.
        std::string clean;
        clean.reserve(result.size());
        for (unsigned char c : result) {
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
            if (c == 0x7F) continue;
            clean += static_cast<char>(c);
        }
        result = std::move(clean);
        const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        if (rc != 0) {
            spdlog::error("[wiki] pdftotext: exited rc={} after {}ms ({}c extracted)",
                          rc, dt, result.size());
            return std::nullopt;
        }
        if (result.empty()) {
            spdlog::error("[wiki] pdftotext: returned 0 chars after {}ms "
                          "(probably scanned PDF without OCR)", dt);
            return std::nullopt;
        }
        spdlog::info("[wiki] pdftotext: extracted {}c in {}ms", result.size(), dt);
        return result;
    }

    return read_raw(p);
}

std::string WikiStore::schema() const
{
    std::lock_guard lk(_mu);
    if (!std::filesystem::exists(_schema_path))
        return std::string(DEFAULT_SCHEMA);
    std::ifstream f(_schema_path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

void WikiStore::update_index(
    const std::vector<std::pair<std::string,std::string>>& entries)
{
    std::lock_guard lk(_mu);
    // Merge: read existing, overwrite entries that appear in the new list.
    std::map<std::string, std::string, std::less<>> existing;
    if (std::filesystem::exists(_index_path)) {
        std::ifstream f(_index_path);
        std::string line;
        while (std::getline(f, line)) {
            // Format: "- [[page-name]] - summary"
            if (!line.starts_with("- [[")) continue;
            auto end = line.find("]]", 4);
            if (end == std::string::npos) continue;
            std::string name = line.substr(4, end - 4);
            std::string summary;
            auto sep = line.find(" - ", end);
            if (sep != std::string::npos) summary = line.substr(sep + 3);
            existing[name] = summary;
        }
    }
    for (auto& [name, summary] : entries)
        existing[name] = summary;

    std::ofstream f(_index_path);
    f << "# Wiki Index\n\n";
    for (auto& [name, summary] : existing)
        f << "- [[" << name << "]] - " << summary << "\n";
}

std::string WikiStore::index_text() const
{
    std::lock_guard lk(_mu);
    if (!std::filesystem::exists(_index_path)) return "# Wiki Index\n\n(empty)\n";
    std::ifstream f(_index_path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

void WikiStore::log_change(std::string_view action, std::string_view page,
                            std::string_view detail)
{
    std::lock_guard lk(_mu);
    auto now = std::chrono::system_clock::now();
    auto ts  = std::format("{:%Y-%m-%d %H:%M:%S}", now);
    std::ofstream f(_changelog_path, std::ios::app);
    f << ts << " | " << action << " | " << page;
    if (!detail.empty()) f << " | " << detail;
    f << "\n";
}

} // namespace astraea::wiki
