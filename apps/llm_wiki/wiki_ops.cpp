#include "wiki_ops.hpp"
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <format>
#include <stdexcept>

namespace astraea::wiki {

// ---------------------------------------------------------------------------
// Glaze structs for parsing LLM JSON output.
// Named namespace required - anonymous namespace breaks glaze external linkage.
// ---------------------------------------------------------------------------
namespace astraea::wiki::ops_json {

struct IngestPage {
    std::string page;
    std::string action;
    std::string summary;
    std::string content;
};

struct LintIssueJson {
    std::string type;
    std::string page;
    std::string description;
    std::string suggested_fix;
};

} // namespace astraea::wiki::ops_json

// ---------------------------------------------------------------------------
// WikiOps
// ---------------------------------------------------------------------------

using namespace astraea::wiki::ops_json;

WikiOps::WikiOps(WikiStore& store, ::astraea::AnthropicClient& client)
    : _store(store), _client(client)
{}

std::string WikiOps::strip_json_fence(const std::string& s)
{
    // Claude often wraps JSON in ```json ... ```. Strip if present.
    auto start = s.find("```json");
    if (start != std::string::npos) {
        start += 7;
        while (start < s.size() && s[start] == '\n') ++start;
        auto end = s.rfind("```");
        if (end != std::string::npos && end > start)
            return s.substr(start, end - start);
    }
    // Also handle plain ``` ... ```
    start = s.find("```");
    if (start != std::string::npos) {
        start += 3;
        while (start < s.size() && s[start] == '\n') ++start;
        auto end = s.rfind("```");
        if (end != std::string::npos && end > start)
            return s.substr(start, end - start);
    }
    // Find the first '[' or '{' as a fallback.
    auto first = s.find_first_of("[{");
    if (first != std::string::npos) return s.substr(first);
    return s;
}

// ---------------------------------------------------------------------------
// Ingest
// ---------------------------------------------------------------------------

drogon::Task<IngestResult>
WikiOps::ingest(std::string_view content, std::string_view source_name)
{
    const std::string system_prompt = std::format(R"(You are a knowledge compiler for an LLM Wiki about NZ tenancy law.

WIKI SCHEMA:
{}

EXISTING INDEX:
{}

Your task: read the source document and return wiki page updates as a JSON array.
Return ONLY the JSON array - no explanation, no markdown fencing.

JSON format:
[
  {{
    "page": "kebab-case-name",
    "action": "create",
    "summary": "one-line summary for the index",
    "content": "full markdown content following the schema structure"
  }}
]

Rules:
- Each page covers ONE concept, entity, or topic
- Use [[page-name]] for all internal links (even if the page does not exist yet)
- Source document name: {}
- Add Sources section citing the source document
- Aim for 3-8 pages per document
- action is "create" for new pages, "update" for pages that already exist in the index
)",
        _store.schema(), _store.index_text(), source_name);

    const std::string user_prompt = std::format(
        "Source document: {}\n\nContent:\n{}", source_name, content);

    spdlog::info("[wiki] ingesting '{}' ({} chars)", source_name, content.size());
    const std::string response = co_await _client.complete(system_prompt, user_prompt);
    auto result = parse_ingest(response, std::string(source_name));
    co_return result;
}

IngestResult WikiOps::parse_ingest(const std::string& raw,
                                    const std::string& source_name)
{
    IngestResult result;
    const std::string json = strip_json_fence(raw);

    std::vector<IngestPage> pages;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(pages, json); e) {
        result.error = "parse failed: " + glz::format_error(e, json);
        spdlog::error("[wiki] ingest parse error: {}", result.error);
        return result;
    }

    std::vector<std::pair<std::string,std::string>> index_updates;
    for (auto& p : pages) {
        if (p.page.empty() || p.content.empty()) continue;
        bool exists = _store.read_page(p.page).has_value();
        _store.write_page(p.page, p.content);
        _store.log_change(exists ? "update" : "create", p.page, source_name);
        result.page_names.push_back(p.page);
        if (exists) ++result.pages_updated;
        else        ++result.pages_created;
        if (!p.summary.empty())
            index_updates.push_back({p.page, p.summary});
    }
    if (!index_updates.empty())
        _store.update_index(index_updates);

    spdlog::info("[wiki] ingest done: {} created, {} updated",
                 result.pages_created, result.pages_updated);
    return result;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

drogon::Task<QueryResult>
WikiOps::query(std::string_view question)
{
    const std::string system_prompt = std::format(R"(You are a knowledgeable assistant for NZ tenancy law with access to a structured wiki.

WIKI CONTENTS:
{}

Answer the question based on the wiki. Cite specific pages using [[page-name]] notation.

If you discover a connection between pages that is not yet recorded in the wiki,
end your answer with exactly this line (optional):
WIKI_UPDATE: [[page-name]]: one-line description of the connection to add

Be concise and practical.
)",
        _store.all_pages_text());

    spdlog::info("[wiki] query: {:.80}", question);
    const std::string response =
        co_await _client.complete(system_prompt, question);

    co_return parse_query(response);
}

QueryResult WikiOps::parse_query(const std::string& raw)
{
    QueryResult result;
    // Split off WIKI_UPDATE line if present.
    const std::string marker = "WIKI_UPDATE:";
    auto pos = raw.rfind(marker);
    if (pos != std::string::npos) {
        result.wiki_update = raw.substr(pos + marker.size());
        // Trim leading whitespace.
        auto it = result.wiki_update.begin();
        while (it != result.wiki_update.end() && (*it == ' ' || *it == '\n')) ++it;
        result.wiki_update.erase(result.wiki_update.begin(), it);
        result.answer = raw.substr(0, pos);
        // Trim trailing whitespace from answer.
        while (!result.answer.empty() && (result.answer.back() == '\n' ||
                                          result.answer.back() == ' '))
            result.answer.pop_back();
    } else {
        result.answer = raw;
    }

    // Extract [[page-name]] citations from the answer.
    const std::string& txt = result.answer;
    std::size_t i = 0;
    while ((i = txt.find("[[", i)) != std::string::npos) {
        auto end = txt.find("]]", i + 2);
        if (end == std::string::npos) break;
        result.pages_used.push_back(txt.substr(i + 2, end - i - 2));
        i = end + 2;
    }
    // Deduplicate.
    std::sort(result.pages_used.begin(), result.pages_used.end());
    result.pages_used.erase(std::unique(result.pages_used.begin(),
                                        result.pages_used.end()),
                            result.pages_used.end());
    return result;
}

// ---------------------------------------------------------------------------
// Lint
// ---------------------------------------------------------------------------

drogon::Task<std::vector<LintIssue>>
WikiOps::lint()
{
    const std::string pages_text = _store.all_pages_text();
    if (pages_text.empty()) co_return {};

    const std::string system_prompt = R"(You are a quality auditor for an LLM Wiki about NZ tenancy law.

Perform a lint check and return issues as a JSON array.
Return ONLY the JSON array - no explanation, no markdown fencing.

JSON format:
[
  {
    "type": "orphan|contradiction|broken_link|outdated|suggestion",
    "page": "page-name",
    "description": "what is wrong",
    "suggested_fix": "how to fix it"
  }
]

Check for:
- orphan: page has no incoming links from other pages
- contradiction: two pages state conflicting facts
- broken_link: [[page-name]] reference to a non-existent page
- outdated: content references superseded legislation or outdated thresholds
- suggestion: structural improvements (missing sections, thin content)
)";

    const std::string user_prompt = "WIKI PAGES:\n" + pages_text;

    spdlog::info("[wiki] lint starting ({} chars)", pages_text.size());
    const std::string response =
        co_await _client.complete(system_prompt, user_prompt);

    co_return parse_lint(response);
}

std::vector<LintIssue> WikiOps::parse_lint(const std::string& raw)
{
    const std::string json = strip_json_fence(raw);
    std::vector<LintIssueJson> parsed;
    if (auto e = glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, json); e) {
        spdlog::error("[wiki] lint parse error: {}", glz::format_error(e, json));
        return {};
    }
    std::vector<LintIssue> out;
    out.reserve(parsed.size());
    for (auto& p : parsed)
        out.push_back({p.type, p.page, p.description, p.suggested_fix});
    return out;
}

} // namespace astraea::wiki
