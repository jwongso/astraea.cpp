#pragma once
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astraea::wiki {

/// Filesystem-backed store for the LLM Wiki.
/// Layout inside data_dir:
///   raw/      - user-supplied source documents (read-only by the wiki)
///   wiki/     - LLM-maintained markdown pages
///   SCHEMA.md - naming/structure rules loaded into every prompt
///
/// All methods are thread-safe.
class WikiStore {
public:
    explicit WikiStore(std::filesystem::path data_dir);

    void init();  // create subdirs; write default SCHEMA.md if absent

    // -- Pages --
    std::optional<std::string> read_page(std::string_view name) const;
    void write_page(std::string_view name, std::string_view content);
    void delete_page(std::string_view name);
    std::vector<std::string> list_pages() const;

    // Concatenated text of all pages for prompt context.
    // Each page is preceded by a "=== page-name ===" header.
    std::string all_pages_text(std::size_t max_chars = 120'000) const;

    // -- Raw sources --
    std::vector<std::filesystem::path> list_raw() const;
    std::optional<std::string> read_raw(const std::filesystem::path& p) const;

    // Like read_raw but extracts text from PDF files via pdftotext.
    // Falls back to plain read for all other extensions.
    std::optional<std::string> read_raw_as_text(const std::filesystem::path& p) const;

    // Save uploaded bytes to raw/filename. Returns the saved path.
    std::filesystem::path save_raw(std::string_view filename, const char* data, std::size_t size);

    // -- Schema --
    std::string schema() const;

    // -- Index --
    // entries: vector of {page_name, one_line_summary}
    void update_index(const std::vector<std::pair<std::string,std::string>>& entries);
    std::string index_text() const;

    // -- Changelog --
    void log_change(std::string_view action, std::string_view page,
                    std::string_view detail = "");

    std::filesystem::path data_dir() const { return _data_dir; }

private:
    std::filesystem::path _data_dir;
    std::filesystem::path _wiki_dir;
    std::filesystem::path _raw_dir;
    std::filesystem::path _schema_path;
    std::filesystem::path _index_path;
    std::filesystem::path _changelog_path;
    mutable std::mutex    _mu;

    std::filesystem::path page_path(std::string_view name) const;
};

} // namespace astraea::wiki
